# Testing

Two tiers, split by what each can actually prove.

```
   /\      on-target   scheduler, queue under load, real peripherals
  /  \
 /____\    host        pure logic — parsers, and later the verdict engine
```

A third tier — system tests against a running board — arrives with the verdict engine, since there is not yet a system-level behaviour worth asserting.

## Host — `pio test -e native`

Runs on your machine with gcc. No board, no network, a couple of seconds.

```sh
pio test -e native
```

| Test | Covers |
| --- | --- |
| `test_native_weather_parse` | TC-1.1 — the response parser, 8 cases |

This tier is why the parser is a free function in its own translation unit with no Arduino headers. The `native` environment compiles only `sources/*/*_parse.cpp`, so anything that includes `Arduino.h` is excluded by construction and the tier stays buildable on a machine with no embedded toolchain at all.

The payoff is being able to test the responses you cannot conveniently produce: a body truncated by a dropped connection, a field the service omitted, a number arriving as a string. Reproducing those against a live API means waiting for a bad day.

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

The consumer prints a heartbeat every 5 seconds when the queue is empty:

```
[  12034] idle     queued=0 dropped=0 stack_free=2196
```

`dropped` climbing means the consumer cannot keep up. `stack_free` is words remaining, not bytes — a number trending toward zero is the warning you get before a stack overflow, which otherwise presents as an unexplained reset.

Silence is the one output that means something is wrong. The heartbeat exists so that "nothing is happening" and "the firmware died" do not look identical.
