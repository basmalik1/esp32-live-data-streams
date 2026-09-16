# core/snapshot

What the fusion stage knows: for every source, the last thing it said and how much that can still be trusted.

```cpp
#include "core/snapshot.h"

enum class SourceStatus { NeverSeen, Ok, Failing, Stale };

struct SourceState {
  bool     everSucceeded;
  Reading  lastGood;            // most recent valid = true
  uint32_t lastAttemptMs;
  bool     lastAttemptOk;
  uint16_t consecutiveFailures;
};
struct Snapshot { SourceState source[SourceId::Count]; };

void         snapshotApply(Snapshot &s, const Reading &r);
uint32_t     readingAgeMs(uint32_t nowMs, uint32_t timestampMs);
uint32_t     staleAfterMs(SourceId id);
SourceStatus snapshotStatus(const Snapshot &s, SourceId id, uint32_t nowMs);
const char  *statusName(SourceStatus s);
```

## No clock, no Arduino

Time arrives as a parameter. That is what puts this module in the host test tier: a source can be made forty-five minutes old, or pushed across the `millis()` wrap, in one line, and the fourteen cases in `test_native_snapshot` run in two seconds.

`readingAgeMs` is an unsigned subtraction, which is why it survives the 49-day rollover — a reading stamped just before the wrap and read just after reports a few hundred milliseconds, not four billion.

## Last-good and last-attempt are separate

`snapshotApply` with a valid reading replaces `lastGood` and clears the failure count. With an invalid one it updates the attempt fields, increments the count, and leaves `lastGood` alone. A failed fetch must not turn a known value into a blank.

## Status

| | |
| --- | --- |
| `NeverSeen` | no successful reading yet; the value fields mean nothing |
| `Ok` | last attempt succeeded, last good value within its threshold |
| `Failing` | last attempt failed, but the last good value is still fresh |
| `Stale` | last good value older than the threshold, regardless of recent attempts |

Checked in that order. Stale outranks failing: old and erroring is old, and the verdict treats it as absent rather than as a recent value with a hiccup. See [verdict](verdict.md) for what each status does to the answer.

## Staleness thresholds

Three missed polls per source: 30 minutes for weather (10-minute poll), 45 for air (15-minute). One miss is a transient; three means the source is gone. The table is in `snapshot.cpp` next to the rule that reads it, so the numbers and the reasoning stay together.
