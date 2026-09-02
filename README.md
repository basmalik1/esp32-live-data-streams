# ESP32 Live Data-Streams

An ESP32-S3 that gathers live data from several online services at once, each on its own schedule, and combines them into a single judgement about current conditions.

The interesting problem is not fetching a forecast. It is that remote services answer at different speeds, fail independently, and are not under your control — so a device that polls them in a loop lets the slowest one set the pace for everything, and one hung request stops the world. This is built as independent FreeRTOS tasks feeding a single queue, which is the shape that makes a stalled source cost nothing but itself.

## How it works

```
sources ──▶ queue ──▶ consumer
```

Sources produce readings on their own cadence; a consumer drains them. Neither knows the other exists — they share only the queue and the shape of a reading.

Every reading carries the time it was obtained and whether the source succeeded. Both matter more than they look: a value with no age cannot be judged stale, and a source that goes silent when it breaks is indistinguishable from one that is merely slow.

```
[  10231] weather  21.3 C  64% RH  11.2 km/h   (age 4 ms)
[  12034] idle     queued=0 dropped=0 stack_free=2196
```

## Status

**v1.0 — the pipeline works end to end.** One source, one queue, one consumer.

| Iteration | Capability | State |
| --- | --- | --- |
| v1.0 | Pipeline — source, queue, consumer | Built |
| v2.0 | Fusion — several sources into one snapshot | Next |
| v3.0 | Verdict — a judgement that degrades when inputs go stale | Planned |
| v4.0 | Streaming and persistence — backpressure and a replayable log | Planned |

The firmware builds and the parser tests pass; nothing has yet run on hardware.

## Hardware

An ESP32-S3 dev board (DevKitC-1 style, ESP32-S3-WROOM-1-N16R8 module). No other components — every input arrives over WiFi.

The board has two USB-C ports and `Serial` comes out the **UART** one, not the native USB port. If the monitor stays blank, that is the first thing to check.

## Getting started

Copy `include/secrets.h.example` to `include/secrets.h` and fill in your WiFi credentials and the coordinates you want conditions for. It is gitignored. The ESP32-S3 only sees 2.4 GHz networks.

```sh
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial, 115200 baud
```

## Tests

```sh
pio test -e native   # 8 tests, no hardware needed
pio test -e target   # on the board
```

The host tier is why the response parser is a free function with no Arduino headers: it can be fed a body truncated by a dropped connection, or one missing a field the service had no data for — the failures that matter and that a live API will not produce on demand. See [docs/testing.md](docs/testing.md).

## Documentation

[docs/](docs/) — [architecture](docs/architecture.md) · [process](docs/process.md) · [testing](docs/testing.md) · [module reference](docs/reference/)

Built as a V-Model exercise: every requirement traced to the code that satisfies it and the test that proves it, including the twelve test cases not yet written. That trail is in [docs/process.md](docs/process.md).
