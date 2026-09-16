# sources/weather

Polls the Open-Meteo forecast API and posts a `Reading` for every attempt.

```cpp
#include "sources/weather/weather.h"
bool weatherSourceStart();
void weatherSourcePollNow();   // fetch now; the cadence restarts from here

#include "sources/weather/weather_parse.h"
bool weatherParse(const char *json, size_t len, WeatherSample &out);
```

## Configuration

Coordinates come from `include/secrets.h`, which is gitignored — coordinates are location data, and a public repository is a poor place to publish where you live. Copy `secrets.h.example` and fill it in.

Open-Meteo needs no API key for non-commercial use.

## Cadence

Ten minutes on success, thirty seconds after a failure, using `vTaskDelayUntil` so the period does not drift by however long the request took. A forecast does not change faster than that, and a failing source should recover quickly without hammering the service.

`PollNow()` aborts the task's current wait. After any wait the task compares the clock to the wake time it was given; waking early means the wait was cut short, and the cadence restarts from the forced poll. That comparison is not optional — leaving the wake time in the future makes `vTaskDelayUntil` skip the next wait entirely, and the on-target test for TC-5.3 caught exactly that.

## Failure behaviour

Every poll posts a reading. A failed fetch posts one with `valid = false` rather than posting nothing.

This is the whole point. A source that goes silent when it breaks looks exactly like a source that is merely slow, and any consumer trying to decide whether its data still means something has no way to tell them apart.

## The parser is separate on purpose

`weather_parse.cpp` includes no Arduino headers, so it compiles and runs on a development machine. That is what makes TC-1.1 possible: eight cases including a truncated body and an omitted field, none of which are convenient to produce against a live service.

It validates every field before using any of them. Open-Meteo omits keys it has no data for rather than sending nulls, and a permissive parser would report `0.0 °C` for a missing temperature — indistinguishable downstream from a real freezing measurement. On any failure the caller's struct is left untouched.

## Known gap

The request goes through `net/https_get`, which calls `setInsecure()`: the connection is encrypted but the server is not authenticated. Pinning a root CA means shipping a certificate that expires. Low exposure for public read-only data, but a real weakness — and kept in one place so it can be fixed in one edit.
