#include "core/snapshot.h"

namespace {

// Three missed polls. One is a transient on a home network; three in a row
// means the source is gone and the last value has stopped describing the
// present. Weather polls every 10 min, air every 15, so the thresholds differ
// - which is the point of holding them per source.
constexpr uint32_t MINUTE_MS = 60 * 1000;
constexpr uint32_t STALE_AFTER_MS[(size_t)SourceId::Count] = {
    30 * MINUTE_MS, // Weather
    45 * MINUTE_MS, // AirQuality
};

} // namespace

const char *statusName(SourceStatus s) {
  switch (s) {
  case SourceStatus::NeverSeen:
    return "never";
  case SourceStatus::Ok:
    return "ok";
  case SourceStatus::Failing:
    return "FAILING";
  case SourceStatus::Stale:
    return "STALE";
  }
  return "?";
}

void snapshotApply(Snapshot &s, const Reading &r) {
  size_t i = (size_t)r.source;
  if (i >= (size_t)SourceId::Count) {
    return;
  }
  SourceState &st = s.source[i];

  st.lastAttemptMs = r.timestampMs;
  st.lastAttemptOk = r.valid;

  if (r.valid) {
    st.lastGood = r;
    st.everSucceeded = true;
    st.consecutiveFailures = 0;
  } else if (st.consecutiveFailures < UINT16_MAX) {
    st.consecutiveFailures++;
  }
}

uint32_t readingAgeMs(uint32_t nowMs, uint32_t timestampMs) {
  return nowMs - timestampMs;
}

uint32_t staleAfterMs(SourceId id) {
  size_t i = (size_t)id;
  return i < (size_t)SourceId::Count ? STALE_AFTER_MS[i] : 0;
}

// Order matters. Stale outranks Failing: a source that is both erroring and
// old is old, and the verdict should treat it as absent, not as a recent
// value with a hiccup.
SourceStatus snapshotStatus(const Snapshot &s, SourceId id, uint32_t nowMs) {
  size_t i = (size_t)id;
  if (i >= (size_t)SourceId::Count) {
    return SourceStatus::NeverSeen;
  }
  const SourceState &st = s.source[i];

  if (!st.everSucceeded) {
    return SourceStatus::NeverSeen;
  }
  if (readingAgeMs(nowMs, st.lastGood.timestampMs) > staleAfterMs(id)) {
    return SourceStatus::Stale;
  }
  if (!st.lastAttemptOk) {
    return SourceStatus::Failing;
  }
  return SourceStatus::Ok;
}
