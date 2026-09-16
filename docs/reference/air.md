# sources/air

Polls the Open-Meteo air-quality API and posts a `Reading` for every attempt.

```cpp
#include "sources/air/air.h"
bool airSourceStart();

#include "sources/air/air_parse.h"
bool airParse(const char *json, size_t len, AirSample &out);
```

Same shape as [weather](weather.md), on purpose. The second source exists to prove that adding one means writing a task and a parser and nothing else.

## Configuration

Same coordinates as weather, from the gitignored `include/secrets.h`. No API key.

## Cadence

Fifteen minutes on success, thirty seconds after a failure. The service updates hourly, so faster polling buys nothing — but the more important property is that fifteen is *not ten*. Two sources on the same period arrive in lockstep and never produce the situation fusion exists for: one input fresh, the other not.

## Fields

`pm2_5` and `pm10` in µg/m³, and `us_aqi`. The AQI is an integer by definition, so the parser rejects a fractional one — it is a sign the response is not what it is assumed to be.

## Failure behaviour

Every poll posts a reading; a failed fetch posts `valid = false`. See weather for why.

Missing-field rejection matters even more here than for weather: `0 µg/m³` is a real and excellent reading, not an obviously wrong one, so a parser that defaulted a missing field to zero would report clean air on a dead response.
