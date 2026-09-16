// Verdict tests: the banding rules and the degradation rules.
//
// Two halves. The first checks what the conditions mean - each factor at each
// band and at its boundaries, and that the worst factor wins. The second walks
// every combination of source status - four states for weather, four for air
// - and checks that the confidence and the set of inputs actually used follow
// the rules, which is what TC-5.1 asks for.

#include <unity.h>

#include "core/verdict.h"

namespace {

constexpr uint32_t MIN = 60u * 1000u;
constexpr uint32_t NOW = 120 * MIN; // past every staleness threshold from 0

// Values that band Good on every factor, so a test that wants to exercise one
// factor can leave the others alone.
constexpr float MILD_C = 20.0f;
constexpr float CALM_KPH = 5.0f;
constexpr int16_t CLEAN_AQI = 20;

Reading weather(uint32_t ts, float tempC, float windKph) {
  Reading r{};
  r.source = SourceId::Weather;
  r.timestampMs = ts;
  r.valid = true;
  r.weather.tempC = tempC;
  r.weather.humidityPct = 50.0f;
  r.weather.windKph = windKph;
  return r;
}

Reading air(uint32_t ts, int16_t aqi) {
  Reading r{};
  r.source = SourceId::AirQuality;
  r.timestampMs = ts;
  r.valid = true;
  r.air.aqi = aqi;
  return r;
}

Reading failed(SourceId id, uint32_t ts) {
  Reading r{};
  r.source = id;
  r.timestampMs = ts;
  r.valid = false;
  return r;
}

// Both sources fresh and good; the baseline every conditions test starts from.
Snapshot baseline(float tempC = MILD_C, float windKph = CALM_KPH,
                  int16_t aqi = CLEAN_AQI) {
  Snapshot s{};
  snapshotApply(s, weather(NOW - MIN, tempC, windKph));
  snapshotApply(s, air(NOW - MIN, aqi));
  return s;
}

bool hasReason(const Verdict &v, Reason r) {
  for (uint8_t i = 0; i < v.reasonCount; i++) {
    if (v.reasons[i] == r) {
      return true;
    }
  }
  return false;
}

// Put one source into a chosen status as of NOW. The value is chosen so that
// if the source is used, it bands Poor - which makes "was it used" visible in
// the outcome.
void putSource(Snapshot &s, SourceId id, SourceStatus want) {
  uint32_t fresh = NOW - MIN;
  uint32_t old = NOW - staleAfterMs(id) - MIN;
  Reading poor = id == SourceId::Weather ? weather(fresh, -10.0f, CALM_KPH)
                                         : air(fresh, 200);
  switch (want) {
  case SourceStatus::NeverSeen:
    snapshotApply(s, failed(id, fresh));
    break;
  case SourceStatus::Ok:
    snapshotApply(s, poor);
    break;
  case SourceStatus::Failing:
    snapshotApply(s, poor);
    snapshotApply(s, failed(id, NOW));
    break;
  case SourceStatus::Stale:
    poor.timestampMs = old;
    snapshotApply(s, poor);
    break;
  }
}

} // namespace

void setUp() {}
void tearDown() {}

// --- Conditions ---------------------------------------------------------------

void test_mild_calm_clean_is_good_with_no_reasons() {
  Verdict v = verdictFrom(baseline(), NOW);
  TEST_ASSERT_EQUAL(Outcome::Good, v.outcome);
  TEST_ASSERT_EQUAL(Confidence::High, v.confidence);
  TEST_ASSERT_EQUAL_UINT8(0, v.reasonCount);
}

void test_temperature_bands_and_boundaries() {
  TEST_ASSERT_EQUAL(Outcome::Good, verdictFrom(baseline(10.0f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair, verdictFrom(baseline(9.9f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Good, verdictFrom(baseline(27.0f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair, verdictFrom(baseline(27.1f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair, verdictFrom(baseline(0.0f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Poor, verdictFrom(baseline(-0.1f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair, verdictFrom(baseline(32.0f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Poor, verdictFrom(baseline(32.1f), NOW).outcome);
}

void test_cold_and_hot_name_themselves() {
  Verdict cold = verdictFrom(baseline(-5.0f), NOW);
  TEST_ASSERT_TRUE(hasReason(cold, Reason::Cold));
  TEST_ASSERT_FALSE(hasReason(cold, Reason::Hot));

  Verdict hot = verdictFrom(baseline(35.0f), NOW);
  TEST_ASSERT_TRUE(hasReason(hot, Reason::Hot));
  TEST_ASSERT_FALSE(hasReason(hot, Reason::Cold));
}

void test_wind_bands_and_boundaries() {
  TEST_ASSERT_EQUAL(Outcome::Good,
                    verdictFrom(baseline(MILD_C, 19.9f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair,
                    verdictFrom(baseline(MILD_C, 20.0f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair,
                    verdictFrom(baseline(MILD_C, 35.0f), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Poor,
                    verdictFrom(baseline(MILD_C, 35.1f), NOW).outcome);
  TEST_ASSERT_TRUE(
      hasReason(verdictFrom(baseline(MILD_C, 25.0f), NOW), Reason::Windy));
}

void test_air_bands_and_boundaries() {
  TEST_ASSERT_EQUAL(Outcome::Good,
                    verdictFrom(baseline(MILD_C, CALM_KPH, 50), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair,
                    verdictFrom(baseline(MILD_C, CALM_KPH, 51), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Fair,
                    verdictFrom(baseline(MILD_C, CALM_KPH, 100), NOW).outcome);
  TEST_ASSERT_EQUAL(Outcome::Poor,
                    verdictFrom(baseline(MILD_C, CALM_KPH, 101), NOW).outcome);
  TEST_ASSERT_TRUE(hasReason(verdictFrom(baseline(MILD_C, CALM_KPH, 75), NOW),
                             Reason::AirPoor));
}

// A mild day with bad air is a bad day. Averaging would hide it.
void test_worst_factor_wins() {
  Verdict v = verdictFrom(baseline(MILD_C, CALM_KPH, 150), NOW);
  TEST_ASSERT_EQUAL(Outcome::Poor, v.outcome);

  Verdict mixed = verdictFrom(baseline(5.0f, 40.0f, CLEAN_AQI), NOW);
  TEST_ASSERT_EQUAL(Outcome::Poor, mixed.outcome); // wind is Poor, temp Fair
  TEST_ASSERT_TRUE(hasReason(mixed, Reason::Cold));
  TEST_ASSERT_TRUE(hasReason(mixed, Reason::Windy));
}

void test_humidity_does_not_move_the_verdict() {
  Snapshot s = baseline();
  s.source[(size_t)SourceId::Weather].lastGood.weather.humidityPct = 100.0f;
  TEST_ASSERT_EQUAL(Outcome::Good, verdictFrom(s, NOW).outcome);
}

// --- Degradation --------------------------------------------------------------

void test_empty_snapshot_is_unknown_with_no_confidence() {
  Snapshot s{};
  Verdict v = verdictFrom(s, NOW);
  TEST_ASSERT_EQUAL(Outcome::Unknown, v.outcome);
  TEST_ASSERT_EQUAL(Confidence::None, v.confidence);
  TEST_ASSERT_TRUE(hasReason(v, Reason::WeatherMissing));
  TEST_ASSERT_TRUE(hasReason(v, Reason::AirMissing));
}

// Fresh last-good value, current attempt failing: the value still counts, the
// confidence does not.
void test_failing_source_counts_at_low_confidence() {
  Snapshot s = baseline(MILD_C, CALM_KPH, 150);
  snapshotApply(s, failed(SourceId::AirQuality, NOW));
  Verdict v = verdictFrom(s, NOW);
  TEST_ASSERT_EQUAL(Outcome::Poor, v.outcome); // the bad air still counts
  TEST_ASSERT_EQUAL(Confidence::Low, v.confidence);
  TEST_ASSERT_TRUE(hasReason(v, Reason::AirFailing));
}

// An old value must not shape the answer. Bad air from an hour ago says
// nothing about now, so it is excluded and the verdict rests on weather.
void test_stale_source_is_excluded() {
  Snapshot s = baseline();
  snapshotApply(s, air(NOW - staleAfterMs(SourceId::AirQuality) - MIN, 200));
  Verdict v = verdictFrom(s, NOW);
  TEST_ASSERT_EQUAL(Outcome::Good, v.outcome);
  TEST_ASSERT_EQUAL(Confidence::Low, v.confidence);
  TEST_ASSERT_TRUE(hasReason(v, Reason::AirStale));
  TEST_ASSERT_FALSE(hasReason(v, Reason::AirPoor));
}

void test_missing_source_is_excluded() {
  Snapshot s{};
  snapshotApply(s, weather(NOW - MIN, MILD_C, CALM_KPH));
  Verdict v = verdictFrom(s, NOW);
  TEST_ASSERT_EQUAL(Outcome::Good, v.outcome);
  TEST_ASSERT_EQUAL(Confidence::Low, v.confidence);
  TEST_ASSERT_TRUE(hasReason(v, Reason::AirMissing));
}

// Network gone: weather goes stale, air goes stale. Nothing usable remains and
// the verdict must say so rather than repeat the last answer.
void test_everything_stale_is_unknown() {
  Snapshot s{};
  snapshotApply(s, weather(0, MILD_C, CALM_KPH));
  snapshotApply(s, air(0, CLEAN_AQI));
  Verdict v = verdictFrom(s, NOW);
  TEST_ASSERT_EQUAL(Outcome::Unknown, v.outcome);
  TEST_ASSERT_EQUAL(Confidence::None, v.confidence);
  TEST_ASSERT_TRUE(hasReason(v, Reason::WeatherStale));
  TEST_ASSERT_TRUE(hasReason(v, Reason::AirStale));
}

// Every combination of source status. Each source, when used, bands Poor, so
// "was it used" shows up as the outcome: Poor if any usable source, Unknown
// if none. Confidence is High only when both are Ok.
void test_every_status_combination() {
  const SourceStatus all[] = {SourceStatus::NeverSeen, SourceStatus::Ok,
                              SourceStatus::Failing, SourceStatus::Stale};
  for (SourceStatus ws : all) {
    for (SourceStatus as : all) {
      Snapshot s{};
      putSource(s, SourceId::Weather, ws);
      putSource(s, SourceId::AirQuality, as);

      // The setup must have produced the status it claims to.
      TEST_ASSERT_EQUAL(ws, snapshotStatus(s, SourceId::Weather, NOW));
      TEST_ASSERT_EQUAL(as, snapshotStatus(s, SourceId::AirQuality, NOW));

      Verdict v = verdictFrom(s, NOW);

      bool wUsable = ws == SourceStatus::Ok || ws == SourceStatus::Failing;
      bool aUsable = as == SourceStatus::Ok || as == SourceStatus::Failing;
      bool bothOk = ws == SourceStatus::Ok && as == SourceStatus::Ok;

      if (!wUsable && !aUsable) {
        TEST_ASSERT_EQUAL(Outcome::Unknown, v.outcome);
        TEST_ASSERT_EQUAL(Confidence::None, v.confidence);
      } else {
        TEST_ASSERT_EQUAL(Outcome::Poor, v.outcome);
        TEST_ASSERT_EQUAL(bothOk ? Confidence::High : Confidence::Low,
                          v.confidence);
      }

      // The condition reasons appear exactly when the source was used.
      TEST_ASSERT_EQUAL(wUsable, hasReason(v, Reason::Cold));
      TEST_ASSERT_EQUAL(aUsable, hasReason(v, Reason::AirPoor));

      // The input reasons appear exactly when the source was not Ok.
      TEST_ASSERT_EQUAL(ws == SourceStatus::Failing,
                        hasReason(v, Reason::WeatherFailing));
      TEST_ASSERT_EQUAL(ws == SourceStatus::Stale,
                        hasReason(v, Reason::WeatherStale));
      TEST_ASSERT_EQUAL(ws == SourceStatus::NeverSeen,
                        hasReason(v, Reason::WeatherMissing));
      TEST_ASSERT_EQUAL(as == SourceStatus::Failing,
                        hasReason(v, Reason::AirFailing));
      TEST_ASSERT_EQUAL(as == SourceStatus::Stale,
                        hasReason(v, Reason::AirStale));
      TEST_ASSERT_EQUAL(as == SourceStatus::NeverSeen,
                        hasReason(v, Reason::AirMissing));
    }
  }
}

void test_reason_count_never_exceeds_capacity() {
  // Worst case: cold, windy, poor air, and both sources failing.
  Snapshot s = baseline(-10.0f, 50.0f, 200);
  snapshotApply(s, failed(SourceId::Weather, NOW));
  snapshotApply(s, failed(SourceId::AirQuality, NOW));
  Verdict v = verdictFrom(s, NOW);
  TEST_ASSERT_EQUAL_UINT8(5, v.reasonCount);
  TEST_ASSERT_TRUE(v.reasonCount <= MAX_REASONS);
}

void test_names_are_printable() {
  TEST_ASSERT_EQUAL_STRING("GOOD", outcomeName(Outcome::Good));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", outcomeName(Outcome::Unknown));
  TEST_ASSERT_EQUAL_STRING("low", confidenceName(Confidence::Low));
  TEST_ASSERT_EQUAL_STRING("air stale", reasonName(Reason::AirStale));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_mild_calm_clean_is_good_with_no_reasons);
  RUN_TEST(test_temperature_bands_and_boundaries);
  RUN_TEST(test_cold_and_hot_name_themselves);
  RUN_TEST(test_wind_bands_and_boundaries);
  RUN_TEST(test_air_bands_and_boundaries);
  RUN_TEST(test_worst_factor_wins);
  RUN_TEST(test_humidity_does_not_move_the_verdict);
  RUN_TEST(test_empty_snapshot_is_unknown_with_no_confidence);
  RUN_TEST(test_failing_source_counts_at_low_confidence);
  RUN_TEST(test_stale_source_is_excluded);
  RUN_TEST(test_missing_source_is_excluded);
  RUN_TEST(test_everything_stale_is_unknown);
  RUN_TEST(test_every_status_combination);
  RUN_TEST(test_reason_count_never_exceeds_capacity);
  RUN_TEST(test_names_are_printable);
  return UNITY_END();
}
