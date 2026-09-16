# Architecture

```
sources ──▶ queue ──▶ fusion ──▶ snapshot ──▶ verdict ──▶ sinks
```

Sources produce; one consumer drains; everything else reads the consumer's state and computes the verdict from it. Sources and sinks share only the shape of a `Reading` and a `Snapshot`, and neither knows the other exists. Everything else follows from that.

```
src/
  core/       reading.h         what flows through the pipeline
              pipeline.{h,cpp}  the queue, and its overflow policy
              snapshot.{h,cpp}  per-source last value, age, status - no Arduino
              verdict.{h,cpp}   snapshot + time → one answer and its reasons - no Arduino
  net/        network_manager   joins WiFi, reports link state
              https_get         one GET over TLS, shared by every source
  sources/
    weather/  weather_parse     response → sample, no Arduino anywhere
              weather           the task that polls and posts, every 10 min
    air/      air_parse         same shape
              air               same shape, every 15 min
  fusion/     fusion            the queue's one consumer; owns the snapshot
  sinks/
    serial/   serial_sink       prints the verdict and the snapshot every 10 s
  main.cpp                      wiring only
```

## Why tasks rather than a loop

Polling several services from one loop makes the slowest one set the pace for all of them, and a single hung request stops everything. Independent tasks remove both problems: each source keeps its own schedule, and a source stuck waiting on a socket blocks nothing but itself.

The two sources here poll on different periods — ten minutes and fifteen — on purpose. Same-period sources would arrive in lockstep and hide exactly the case fusion exists for: one input fresh, the other not.

The cost is that concurrency becomes real. Three things follow from that and are handled deliberately.

## The queue is the only coupling between sources and the rest

Sources call `pipelinePost()`; the fusion task calls `pipelineTake()`. Adding a source means writing a task and posting the same struct — nothing downstream changes.

**Overflow policy is drop-oldest**, and the reasoning matters more than the choice:

- *Blocking the producer* would stall a source that may be driven by hardware with its own timing. Rejected outright.
- *Dropping the newest* freezes the picture at the moment the consumer stalled, which is the worst possible answer — the data looks present and is wrong.
- *Dropping the oldest* costs history and keeps current state, which for telemetry is the right trade.

The fast path takes no lock, so producers never serialise against each other. A mutex is taken only when the queue is full, because making room is a discard followed by a send — two operations, and two producers arriving together would otherwise discard two entries to admit two.

Drops are counted and reported. A climbing drop count is a design fact about the consumer being too slow, and hiding it would only delay finding out.

## The queue has one consumer; everything else reads the snapshot

A FreeRTOS queue hands each element to exactly one receiver. Two tasks draining the same queue would each see half the readings, so the fusion task is the only one that takes, and it folds every reading into a `Snapshot` under a mutex. Anything that wants to know the state of the world — the serial sink today, the verdict engine and the log later — calls `fusionSnapshot()` and gets a copy.

The copy is the point. It is about a hundred bytes, the lock is held for a struct assignment, and the caller then reads at leisure without holding anything. The verdict can be a pure function of a `Snapshot` and a time, which is what makes it testable on the host.

## The snapshot keeps last-good and last-attempt apart

```cpp
struct SourceState {
  bool     everSucceeded;
  Reading  lastGood;            // most recent valid = true
  uint32_t lastAttemptMs;
  bool     lastAttemptOk;
  uint16_t consecutiveFailures;
};
```

A failed fetch updates the attempt fields and leaves `lastGood` alone. From those, and the current time, a source is in one of four states:

| Status | Meaning |
| --- | --- |
| `NeverSeen` | no successful reading yet — the value fields mean nothing |
| `Ok` | the last attempt succeeded and the value is within its staleness threshold |
| `Failing` | the last attempt failed, but the last good value is still fresh enough to use |
| `Stale` | the last good value is older than the threshold, whether or not attempts are succeeding |

Stale outranks failing: a source that is both old and erroring is old, and should be treated as absent rather than as a recent value with a hiccup.

**Staleness is three missed polls**, per source: 30 minutes for weather, 45 for air. One miss is a transient on a home network; three in a row means the source is gone and its last value has stopped describing the present. The thresholds sit in `snapshot.cpp` next to the rule that uses them.

The snapshot module has no Arduino headers and no clock — time is a parameter — so all of this runs on the host, where a source can be made forty-five minutes old in one line.

## The verdict is a view of the snapshot

`verdictFrom(snapshot, nowMs)` is a pure function. There is no verdict task and no stored verdict, because there is nothing to store: the snapshot is the state, and the verdict is what it means right now. Any consumer with a snapshot copy computes it on demand — the serial sink on every tick, the log later from a recorded snapshot, which is what makes replay reproduce the original answers.

Each factor is banded and the verdict is the **worst band**. Someone asking "is it good out" wants to know about the thing that would ruin it.

| Factor | Good | Fair | Poor |
| --- | --- | --- | --- |
| Temperature | 10 – 27 °C | 0 – 10, 27 – 32 | below 0, above 32 |
| Wind | under 20 km/h | 20 – 35 | above 35 |
| US AQI | ≤ 50 | 51 – 100 | above 100 |

Source status decides whether a factor is allowed to count:

| Status | Its factors | Confidence |
| --- | --- | --- |
| `Ok` | count | — |
| `Failing` | count — the last good value is still fresh | Low |
| `Stale` | excluded | Low |
| `NeverSeen` | excluded | Low |

Confidence is `High` only when every source is `Ok`. When nothing is usable the outcome is `Unknown` — the verdict says it does not know rather than repeating the last answer, which is the failure REQ-5 exists to prevent.

## Readings carry their own age

```cpp
struct Reading {
  SourceId source;
  uint32_t timestampMs;  // when the value was obtained
  bool     valid;        // false = the source is reporting a failure
  union { WeatherSample weather; AirSample air; };
};
```

Three decisions in eight lines:

**The timestamp is taken after the fetch, not before.** A slow request would otherwise look fresher than it is, and age is the only thing a consumer can use to decide whether a value still means anything.

**Failures are posted, not swallowed.** A source that goes quiet when it breaks is indistinguishable downstream from one that is merely slow. Posting `valid = false` makes the difference explicit at the point where it is still cheap to act on — and it is what lets the snapshot say *failing* rather than just *old*.

**The payload is a union**, so the queue element size is fixed now while there is nothing to break. Discovering later that the largest payload grew means resizing the queue and re-testing its overflow behaviour under load.

## Scheduling

| | |
| --- | --- |
| Cadence | `vTaskDelayUntil`, so the period does not drift by however long the fetch took |
| Core | Application tasks pinned to core 1; WiFi and lwIP own core 0 |
| Priority | Sources (3) above fusion (2) above the sink (1) — producing is more time-sensitive than folding, which is more time-sensitive than printing |
| Stacks | Declared explicitly per task; every task reports its own high-water mark |

`loop()` is empty and suspends itself. Every unit of work lives in a task with its own stack, priority and schedule, so the Arduino loop task has nothing to do and hands its slice back.

## What comes next, and what will not change

The log sink attaches at the snapshot the same way the serial sink does: take a copy, record it, and on replay recompute the verdict from what was recorded. The push source at v4.0 attaches at the queue like any other source — and is the first thing that will make the queue's depth and overflow policy matter. Neither touches the `Reading` contract, the fusion task or the verdict function, which is the point of settling those now.
