# Testing

Two tiers, split by what each can actually prove.

```
   /\      on-target   scheduler, queue under load, real peripherals
  /  \
 /____\    host        pure logic — parsers, the snapshot, the verdict
```

A third tier — system tests against a running board — now has something to assert: TC-5.3 pulls the network mid-run and expects the verdict to go *Unknown* rather than stay confident. It needs the board.

## Host — `pio test -e native`

Runs on your machine with gcc. No board, no network, a couple of seconds.

```sh
pio test -e native
```

| Test | Covers |
| --- | --- |
| `test_native_weather_parse` | TC-1.1 — the forecast parser, 8 cases |
| `test_native_air_parse` | TC-1.1 — the air-quality parser, 9 cases |
| `test_native_snapshot` | TC-2.1, TC-2.3, TC-3.3 — age arithmetic and the staleness rules, 14 cases |
| `test_native_verdict` | TC-5.1 — banding, worst-factor-wins, and every status combination, 15 cases |

This tier is why the parsers, the snapshot and the verdict are free functions in their own translation units with no Arduino headers. The `native` environment compiles only `sources/*/*_parse.cpp`, `core/snapshot.cpp` and `core/verdict.cpp`, so anything that includes `Arduino.h` is excluded by construction and the tier stays buildable on a machine with no embedded toolchain at all.

The payoff is being able to test what you cannot conveniently produce. For the parsers: a body truncated by a dropped connection, a field the service omitted, a number arriving as a string. For the snapshot: a source that is forty-five minutes old, a timestamp on the far side of the 49-day `millis()` wrap, a failure landing on top of a good value. For the verdict: all sixteen combinations of source status, and a day that is mild, calm and has an AQI of 150. Reproducing any of those against a live board means waiting for a bad day, or a long one.

## On-target — `pio test -e target`

Builds a test firmware, flashes it, and reads Unity's results back over serial. Needs the board connected.

```sh
pio test -e target
```

No tests yet. The ones this tier is for are the ones a host cannot answer honestly:

- **Queue overflow under a real flood** — a producer task genuinely outrunning a consumer task, not a simulated one.
- **Producers never blocking**, measured as cadence holding while the queue is saturated.
- **Stack headroom** via `uxTaskGetStackHighWaterMark`, so stack sizes become measurements rather than estimates.

Budget roughly 25 seconds per test file: each is a full build, flash and run cycle.

`main.cpp` is excluded from these builds, because its `setup()`/`loop()` would collide with the test runner's.

## Reading the running system

Three kinds of line. The fusion task prints one per arrival, which is the observable for "a reading made it through the queue":

```
[  10231] weather  21.3 C  64% RH  11.2 km/h
[  10502] air      pm2.5 3.4  pm10 6.3  aqi 26
[  40511] air      FAILED
```

The sink prints the verdict and the snapshot every 10 seconds — the answer, then per source its status, last good value and age:

```
[  50000] snapshot queued=0 dropped=0 stack_free=2840
  verdict  GOOD      confidence low   air failing
  weather  ok        21.3 C  64% RH  11.2 km/h  age 39s
  air      FAILING   pm2.5 3.4  pm10 6.3  aqi 26  age 39s  (1 failed since)
```

And when nothing has arrived for 5 seconds, fusion prints a heartbeat:

```
[  12034] idle     queued=0 dropped=0 stack_free=2196
```

`dropped` climbing means the consumer cannot keep up. `stack_free` is words remaining, not bytes — a number trending toward zero is the warning you get before a stack overflow, which otherwise presents as an unexplained reset. Each source task prints its own `stack_free` after every fetch, which is the moment it is lowest.

Silence is the one output that means something is wrong. The heartbeat exists so that "nothing is happening" and "the firmware died" do not look identical.
