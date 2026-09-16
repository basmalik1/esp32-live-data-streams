# core/verdict

The one answer the device exists to give — *is it good out right now?* — and the reasons behind it.

```cpp
#include "core/verdict.h"

enum class Outcome    { Unknown, Good, Fair, Poor };
enum class Confidence { None, Low, High };
enum class Reason     { Cold, Hot, Windy, AirPoor,
                        WeatherFailing, WeatherStale, WeatherMissing,
                        AirFailing, AirStale, AirMissing };

struct Verdict {
  Outcome    outcome;
  Confidence confidence;
  uint8_t    reasonCount;
  Reason     reasons[MAX_REASONS];
};

Verdict verdictFrom(const Snapshot &s, uint32_t nowMs);
```

## Computed, not stored

A pure function of a snapshot and a time. No task, no lock, no stored result. The snapshot already is the state; the verdict is a view of it, and any consumer holding a snapshot copy computes it on demand. The serial sink does so on every tick. The log will do the same from a recorded snapshot, which is what lets a replay reproduce the original verdicts exactly.

## Banding

Each factor is banded Good / Fair / Poor. The verdict is the worst band — someone asking "is it good out" wants to hear about the thing that would ruin it, and averaging would let a mild temperature hide a bad air day.

| Factor | Good | Fair | Poor |
| --- | --- | --- | --- |
| Temperature | 10 – 27 °C | 0 – 10, 27 – 32 | below 0, above 32 |
| Wind | under 20 km/h | 20 – 35 | above 35 |
| US AQI | ≤ 50 | 51 – 100 | above 100 |

Boundaries are inclusive on the Good side: 10.0 °C is Good, 9.9 is Fair. The AQI bands are the EPA's. Wind follows the Beaufort scale — 3 to 4 goes unnoticed, 5 is noticeable, 6 and up is unpleasant. Temperature is comfortable / jacket-or-sweat / hazardous.

Humidity is fetched and deliberately unused. A felt-temperature formula needs calibration this project has no way to verify, and an unverifiable threshold is worse than none.

## Degradation

| Source status | Its factors | Confidence | Reason |
| --- | --- | --- | --- |
| `Ok` | count | — | — |
| `Failing` | count — the last good value is still fresh | Low | `weather failing` / `air failing` |
| `Stale` | excluded | Low | `weather stale` / `air stale` |
| `NeverSeen` | excluded | Low | `weather missing` / `air missing` |

Confidence is `High` only when every source is `Ok`. When no source is usable the outcome is `Unknown` with confidence `None` — the verdict says it does not know rather than guessing from nothing, or repeating the last answer.

## Reasons

A fixed array, never the heap: the verdict runs on every print tick. Input reasons come first, condition reasons second, so a printed list reads "what we could not check, then what we found". The worst case — cold, windy, poor air, both sources failing — is five.
