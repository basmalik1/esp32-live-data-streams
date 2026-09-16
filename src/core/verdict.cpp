#include "core/verdict.h"

namespace {

// --- Thresholds ---------------------------------------------------------------
//
// Each factor is banded Good / Fair / Poor and the verdict is the worst band.
// Boundaries are inclusive on the Good side: 10.0 C is Good, 9.9 is Fair.
//
// Temperature: comfortable / jacket-or-sweat / hazardous.
constexpr float TEMP_GOOD_MIN_C = 10.0f;
constexpr float TEMP_GOOD_MAX_C = 27.0f;
constexpr float TEMP_FAIR_MIN_C = 0.0f;
constexpr float TEMP_FAIR_MAX_C = 32.0f;

// Wind: Beaufort 3-4 goes unnoticed, 5 is noticeable, 6 and up is unpleasant.
constexpr float WIND_GOOD_MAX_KPH = 20.0f; // exclusive
constexpr float WIND_FAIR_MAX_KPH = 35.0f; // inclusive

// US AQI, straight from the EPA bands: Good, Moderate, and everything from
// "unhealthy for sensitive groups" upward.
constexpr int16_t AQI_GOOD_MAX = 50;
constexpr int16_t AQI_FAIR_MAX = 100;

// Humidity is fetched and deliberately unused. A felt-temperature formula
// needs calibration this project has no way to verify, and an unverifiable
// threshold is worse than none.

// --- Banding ------------------------------------------------------------------

Outcome worse(Outcome a, Outcome b) { return a > b ? a : b; }

void addReason(Verdict &v, Reason r) {
  if (v.reasonCount < MAX_REASONS) {
    v.reasons[v.reasonCount++] = r;
  }
}

Outcome bandTemperature(float c, Verdict &v) {
  if (c >= TEMP_GOOD_MIN_C && c <= TEMP_GOOD_MAX_C) {
    return Outcome::Good;
  }
  addReason(v, c < TEMP_GOOD_MIN_C ? Reason::Cold : Reason::Hot);
  if (c >= TEMP_FAIR_MIN_C && c <= TEMP_FAIR_MAX_C) {
    return Outcome::Fair;
  }
  return Outcome::Poor;
}

Outcome bandWind(float kph, Verdict &v) {
  if (kph < WIND_GOOD_MAX_KPH) {
    return Outcome::Good;
  }
  addReason(v, Reason::Windy);
  return kph <= WIND_FAIR_MAX_KPH ? Outcome::Fair : Outcome::Poor;
}

Outcome bandAir(int16_t aqi, Verdict &v) {
  if (aqi <= AQI_GOOD_MAX) {
    return Outcome::Good;
  }
  addReason(v, Reason::AirPoor);
  return aqi <= AQI_FAIR_MAX ? Outcome::Fair : Outcome::Poor;
}

// --- Degradation --------------------------------------------------------------
//
// Failing: the last good value is still fresh, so it counts - at low
// confidence, and with a reason saying why.
// Stale or never seen: excluded. An old value or no value must not shape the
// answer; it shapes the confidence instead.
//
// Returns whether the source's value may be used.
bool admit(SourceStatus status, Reason failing, Reason stale, Reason missing,
           Verdict &v, bool &anyUsable, bool &anyDegraded) {
  switch (status) {
  case SourceStatus::Ok:
    anyUsable = true;
    return true;
  case SourceStatus::Failing:
    addReason(v, failing);
    anyUsable = true;
    anyDegraded = true;
    return true;
  case SourceStatus::Stale:
    addReason(v, stale);
    anyDegraded = true;
    return false;
  case SourceStatus::NeverSeen:
    addReason(v, missing);
    anyDegraded = true;
    return false;
  }
  return false;
}

} // namespace

Verdict verdictFrom(const Snapshot &s, uint32_t nowMs) {
  Verdict v{};
  v.outcome = Outcome::Good;

  bool anyUsable = false;
  bool anyDegraded = false;

  // Input reasons are added first, condition reasons second, so a printed
  // list reads "what we know, and what we could not check" in a stable order.
  bool useWeather =
      admit(snapshotStatus(s, SourceId::Weather, nowMs), Reason::WeatherFailing,
            Reason::WeatherStale, Reason::WeatherMissing, v, anyUsable,
            anyDegraded);
  bool useAir =
      admit(snapshotStatus(s, SourceId::AirQuality, nowMs), Reason::AirFailing,
            Reason::AirStale, Reason::AirMissing, v, anyUsable, anyDegraded);

  if (!anyUsable) {
    v.outcome = Outcome::Unknown;
    v.confidence = Confidence::None;
    return v;
  }

  if (useWeather) {
    const WeatherSample &w =
        s.source[(size_t)SourceId::Weather].lastGood.weather;
    v.outcome = worse(v.outcome, bandTemperature(w.tempC, v));
    v.outcome = worse(v.outcome, bandWind(w.windKph, v));
  }
  if (useAir) {
    const AirSample &a = s.source[(size_t)SourceId::AirQuality].lastGood.air;
    v.outcome = worse(v.outcome, bandAir(a.aqi, v));
  }

  v.confidence = anyDegraded ? Confidence::Low : Confidence::High;
  return v;
}

const char *outcomeName(Outcome o) {
  switch (o) {
  case Outcome::Unknown:
    return "UNKNOWN";
  case Outcome::Good:
    return "GOOD";
  case Outcome::Fair:
    return "FAIR";
  case Outcome::Poor:
    return "POOR";
  }
  return "?";
}

const char *confidenceName(Confidence c) {
  switch (c) {
  case Confidence::None:
    return "none";
  case Confidence::Low:
    return "low";
  case Confidence::High:
    return "high";
  }
  return "?";
}

const char *reasonName(Reason r) {
  switch (r) {
  case Reason::Cold:
    return "cold";
  case Reason::Hot:
    return "hot";
  case Reason::Windy:
    return "windy";
  case Reason::AirPoor:
    return "poor air";
  case Reason::WeatherFailing:
    return "weather failing";
  case Reason::WeatherStale:
    return "weather stale";
  case Reason::WeatherMissing:
    return "weather missing";
  case Reason::AirFailing:
    return "air failing";
  case Reason::AirStale:
    return "air stale";
  case Reason::AirMissing:
    return "air missing";
  }
  return "?";
}
