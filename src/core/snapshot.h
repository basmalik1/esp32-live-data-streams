#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/reading.h"

// The fusion stage's view of the world: for every source, the last thing it
// said and how much that can still be trusted.
//
// No Arduino headers, and time arrives as a parameter rather than being read
// from millis(). That is what lets the age arithmetic and the staleness rules
// run on the host, where a source can be made "45 minutes old" in one line.

enum class SourceStatus : uint8_t {
  NeverSeen, // no successful reading yet - the value fields mean nothing
  Ok,        // last attempt succeeded and the value is within its threshold
  Failing,   // last attempt failed, but the last good value is still fresh
  Stale,     // the last good value is older than the source's threshold
};

const char *statusName(SourceStatus s);

// Last good and last attempt are held separately on purpose. A failure must
// not erase the last known value - a verdict wants "what we last knew, and
// how long ago" far more than it wants a blank.
struct SourceState {
  bool everSucceeded;
  Reading lastGood; // most recent reading with valid = true
  uint32_t lastAttemptMs;
  bool lastAttemptOk;
  uint16_t consecutiveFailures;
};

struct Snapshot {
  SourceState source[(size_t)SourceId::Count];
};

// Fold one reading into the snapshot. Pure: no locking, no clock.
void snapshotApply(Snapshot &s, const Reading &r);

// Unsigned subtraction, so it stays correct across the 49-day millis() wrap.
uint32_t readingAgeMs(uint32_t nowMs, uint32_t timestampMs);

// How old a source's last good value may be before it stops counting.
uint32_t staleAfterMs(SourceId id);

SourceStatus snapshotStatus(const Snapshot &s, SourceId id, uint32_t nowMs);
