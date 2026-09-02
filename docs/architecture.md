# Architecture

```
sources ──▶ queue ──▶ consumer
```

Sources produce; a consumer drains. Neither knows the other exists — they share only the queue and the shape of a `Reading`. Everything else follows from that.

```
src/
  core/       reading.h        what flows through the pipeline
              pipeline.{h,cpp} the queue, and its overflow policy
  net/        network_manager  joins WiFi, reports link state
  sources/
    weather/  weather_parse    response → sample, no Arduino anywhere
              weather          the task that polls and posts
  sinks/
    serial/   serial_sink      the consumer
  main.cpp                     wiring only
```

## Why tasks rather than a loop

Polling three services from one loop makes the slowest one set the pace for all of them, and a single hung request stops everything. Independent tasks remove both problems: each source keeps its own schedule, and a source stuck waiting on a socket blocks nothing but itself.

The cost is that concurrency becomes real. Two things follow from that and are handled deliberately.

## The queue is the only coupling

Sources call `pipelinePost()`; the consumer calls `pipelineTake()`. Adding a source means writing a task and posting the same struct — no consumer changes. Adding a consumer means draining the same queue.

**Overflow policy is drop-oldest**, and the reasoning matters more than the choice:

- *Blocking the producer* would stall a source that may be driven by hardware with its own timing. Rejected outright.
- *Dropping the newest* freezes the picture at the moment the consumer stalled, which is the worst possible answer — the data looks present and is wrong.
- *Dropping the oldest* costs history and keeps current state, which for telemetry is the right trade.

The fast path takes no lock, so producers never serialise against each other. A mutex is taken only when the queue is full, because making room is a discard followed by a send — two operations, and two producers arriving together would otherwise discard two entries to admit two.

Drops are counted and reported. A climbing drop count is a design fact about the consumer being too slow, and hiding it would only delay finding out.

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

**Failures are posted, not swallowed.** A source that goes quiet when it breaks is indistinguishable downstream from one that is merely slow. Posting `valid = false` makes the difference explicit at the point where it is still cheap to act on.

**The payload is a union**, so the queue element size is fixed now while there is nothing to break. Discovering later that the largest payload grew means resizing the queue and re-testing its overflow behaviour under load.

## Scheduling

| | |
| --- | --- |
| Cadence | `vTaskDelayUntil`, so the period does not drift by however long the fetch took |
| Core | Application tasks pinned to core 1; WiFi and lwIP own core 0 |
| Priority | Sources above the consumer — producing is more time-sensitive than printing |
| Stacks | Declared explicitly per task; the consumer reports its own high-water mark |

`loop()` is empty and suspends itself. Every unit of work lives in a task with its own stack, priority and schedule, so the Arduino loop task has nothing to do and hands its slice back.

## What comes next, and what will not change

The fusion stage, the verdict engine and the log sink all attach at the queue. Sources gain a second and third member; the consumer becomes a fusion task that maintains a snapshot with per-field timestamps.

None of that requires the `Reading` contract or the queue semantics to change — which is the point of settling them now, while the only thing that can break is a weather reading.
