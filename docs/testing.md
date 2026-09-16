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

| Test | Covers |
| --- | --- |
| `test_embedded_pipeline` | TC-4.1, TC-4.2 — overflow drops the oldest; two producers flooding a saturated queue never wait more than microseconds |
| `test_embedded_sources` | TC-3.1, TC-3.2 — a fetch with no network posts `valid = false`; a 404 is a failure, not silence |
| `test_embedded_verdict` | TC-5.2, TC-5.3 — a stale reading through the live pipeline is excluded; pulling the network mid-run drops the live verdict to low confidence |

These are the questions a host cannot answer honestly: they are about the real scheduler, the real queue, real TLS and a real network going away. The last one earned its place immediately — it caught a double-fetch after a forced poll that no host test could have seen, because the cause was `vTaskDelayUntil`'s treatment of a wake time in the future.

Budget roughly 30 seconds per test file: each is a full build, flash and run cycle, and two of the three join WiFi. The test task waits 3 s at boot so the runner has time to reopen the port after the upload reset; without that the first case's result line can be printed before anyone is listening.

`main.cpp` is excluded from these builds, because its `setup()`/`loop()` would collide with the test runner's.

## Soak — `read_serial` for half an hour

TC-1.3 is not a Unity test. It is the main firmware left running while a script reads the UART port for at least three weather periods, and then the arrival timestamps checked against 600 s and 900 s. The result — under 0.4 s of accumulated offset after half an hour — is in the process document.

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

`dropped` climbing means the consumer cannot keep up. `stack_free` is **bytes** remaining — ESP-IDF's FreeRTOS port reports the high-water mark in bytes, unlike vanilla FreeRTOS, which reports words. (An earlier version of this page said words. It was written before the firmware had run, and the first run was what prompted checking the header.) A number trending toward zero is the warning you get before a stack overflow, which otherwise presents as an unexplained reset. Each source task prints its own `stack_free` after every fetch, which is the moment it is lowest.

Silence is the one output that means something is wrong. The heartbeat exists so that "nothing is happening" and "the firmware died" do not look identical.
