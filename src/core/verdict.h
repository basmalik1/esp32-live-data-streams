#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/snapshot.h"

// The one answer the device exists to give - is it good out right now? - and
// the reasons behind it.
//
// A pure function of a snapshot and a time. No task, no lock, no stored
// result: the snapshot already is the state, and the verdict is a view of it.
// Any consumer holding a snapshot copy computes it on demand, and the log can
// later recompute it from a recorded snapshot and get the same answer.

enum class Outcome : uint8_t {
  Unknown, // nothing usable to judge from
  Good,
  Fair,
  Poor,
};

enum class Confidence : uint8_t {
  None, // no source is usable
  Low,  // at least one source is failing, stale or missing
  High, // every source is ok
};

enum class Reason : uint8_t {
  // What the conditions are.
  Cold,
  Hot,
  Windy,
  AirPoor,
  // What could not be trusted.
  WeatherFailing,
  WeatherStale,
  WeatherMissing,
  AirFailing,
  AirStale,
  AirMissing,
};

// Fixed array rather than anything dynamic: the verdict is computed on every
// print tick, and allocation has no business on that path.
constexpr size_t MAX_REASONS = 8;

struct Verdict {
  Outcome outcome;
  Confidence confidence;
  uint8_t reasonCount;
  Reason reasons[MAX_REASONS];
};

Verdict verdictFrom(const Snapshot &s, uint32_t nowMs);

const char *outcomeName(Outcome o);
const char *confidenceName(Confidence c);
const char *reasonName(Reason r);
