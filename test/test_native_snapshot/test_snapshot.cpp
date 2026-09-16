// Snapshot tests: age arithmetic and the staleness rules.
//
// The snapshot takes time as a parameter, so a source can be made forty-five
// minutes old, or pushed across the millis() wrap, in one line. Neither is
// something you would want to wait for on a board.

#include <unity.h>

#include "core/snapshot.h"

namespace {

constexpr uint32_t MIN = 60u * 1000u;

Reading good(SourceId id, uint32_t ts, float value) {
  Reading r{};
  r.source = id;
  r.timestampMs = ts;
  r.valid = true;
  if (id == SourceId::Weather) {
    r.weather.tempC = value;
  } else {
    r.air.pm25 = value;
  }
  return r;
}

Reading failed(SourceId id, uint32_t ts) {
  Reading r{};
  r.source = id;
  r.timestampMs = ts;
  r.valid = false;
  return r;
}

const SourceState &weather(const Snapshot &s) {
  return s.source[(size_t)SourceId::Weather];
}

} // namespace

void setUp() {}
void tearDown() {}

// --- TC-2.1: age across the rollover -----------------------------------------

void test_age_is_elapsed_time() {
  TEST_ASSERT_EQUAL_UINT32(5000, readingAgeMs(15000, 10000));
}

// millis() wraps after ~49.7 days. A reading stamped just before the wrap and
// read just after must report a small age, not four billion milliseconds.
void test_age_survives_millis_rollover() {
  uint32_t before = 0xFFFFFF00u;
  uint32_t after = 0x00000100u; // 512 ms later, past the wrap
  TEST_ASSERT_EQUAL_UINT32(512, readingAgeMs(after, before));
}

// --- TC-2.3: ageing and classification ---------------------------------------

void test_empty_snapshot_has_never_seen_anything() {
  Snapshot s{};
  TEST_ASSERT_EQUAL(SourceStatus::NeverSeen,
                    snapshotStatus(s, SourceId::Weather, 0));
  TEST_ASSERT_EQUAL(SourceStatus::NeverSeen,
                    snapshotStatus(s, SourceId::AirQuality, 0));
}

void test_good_reading_is_ok_and_keeps_its_value() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 1000, 21.3f));
  TEST_ASSERT_EQUAL(SourceStatus::Ok,
                    snapshotStatus(s, SourceId::Weather, 2000));
  TEST_ASSERT_TRUE(weather(s).everSucceeded);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.3f, weather(s).lastGood.weather.tempC);
  TEST_ASSERT_EQUAL_UINT32(1000, weather(s).lastGood.timestampMs);
}

void test_reading_goes_stale_past_its_threshold() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 0, 21.3f));
  uint32_t limit = staleAfterMs(SourceId::Weather);
  TEST_ASSERT_EQUAL(SourceStatus::Ok,
                    snapshotStatus(s, SourceId::Weather, limit));
  TEST_ASSERT_EQUAL(SourceStatus::Stale,
                    snapshotStatus(s, SourceId::Weather, limit + 1));
}

// A stale reading is still there. The verdict decides what to do with an old
// value; the snapshot's job is only to say how old it is.
void test_stale_reading_still_carries_its_last_value() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 0, 21.3f));
  uint32_t later = staleAfterMs(SourceId::Weather) + 5 * MIN;
  TEST_ASSERT_EQUAL(SourceStatus::Stale,
                    snapshotStatus(s, SourceId::Weather, later));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.3f, weather(s).lastGood.weather.tempC);
  TEST_ASSERT_EQUAL_UINT32(later, readingAgeMs(later, weather(s).lastGood.timestampMs));
}

void test_thresholds_differ_per_source() {
  TEST_ASSERT_NOT_EQUAL(staleAfterMs(SourceId::Weather),
                        staleAfterMs(SourceId::AirQuality));
}

void test_sources_age_independently() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 0, 21.3f));
  snapshotApply(s, good(SourceId::AirQuality, 40 * MIN, 3.4f));
  uint32_t now = 41 * MIN; // weather is 41 min old, air is 1 min old
  TEST_ASSERT_EQUAL(SourceStatus::Stale,
                    snapshotStatus(s, SourceId::Weather, now));
  TEST_ASSERT_EQUAL(SourceStatus::Ok,
                    snapshotStatus(s, SourceId::AirQuality, now));
}

void test_newer_reading_replaces_older() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 1000, 21.3f));
  snapshotApply(s, good(SourceId::Weather, 2000, 22.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, weather(s).lastGood.weather.tempC);
  TEST_ASSERT_EQUAL_UINT32(2000, weather(s).lastGood.timestampMs);
}

// --- TC-3.3: failure is distinguishable from silence -------------------------

void test_failure_after_good_reading_is_failing_not_blank() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 0, 21.3f));
  snapshotApply(s, failed(SourceId::Weather, 1 * MIN));
  TEST_ASSERT_EQUAL(SourceStatus::Failing,
                    snapshotStatus(s, SourceId::Weather, 2 * MIN));
  // The last good value survives the failure.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.3f, weather(s).lastGood.weather.tempC);
  TEST_ASSERT_EQUAL_UINT32(0, weather(s).lastGood.timestampMs);
  TEST_ASSERT_EQUAL_UINT16(1, weather(s).consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1 * MIN, weather(s).lastAttemptMs);
}

void test_failure_before_any_good_reading_is_still_never_seen() {
  Snapshot s{};
  snapshotApply(s, failed(SourceId::Weather, 1000));
  snapshotApply(s, failed(SourceId::Weather, 2000));
  TEST_ASSERT_EQUAL(SourceStatus::NeverSeen,
                    snapshotStatus(s, SourceId::Weather, 3000));
  TEST_ASSERT_FALSE(weather(s).everSucceeded);
  TEST_ASSERT_EQUAL_UINT16(2, weather(s).consecutiveFailures);
}

// Old and erroring is old. The verdict should see an absent input, not a
// recent value with a hiccup.
void test_stale_outranks_failing() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 0, 21.3f));
  uint32_t later = staleAfterMs(SourceId::Weather) + 1 * MIN;
  snapshotApply(s, failed(SourceId::Weather, later));
  TEST_ASSERT_EQUAL(SourceStatus::Stale,
                    snapshotStatus(s, SourceId::Weather, later));
}

void test_recovery_clears_the_failure_count() {
  Snapshot s{};
  snapshotApply(s, failed(SourceId::Weather, 1000));
  snapshotApply(s, failed(SourceId::Weather, 2000));
  snapshotApply(s, good(SourceId::Weather, 3000, 21.3f));
  TEST_ASSERT_EQUAL(SourceStatus::Ok,
                    snapshotStatus(s, SourceId::Weather, 4000));
  TEST_ASSERT_EQUAL_UINT16(0, weather(s).consecutiveFailures);
}

void test_failure_in_one_source_does_not_touch_the_other() {
  Snapshot s{};
  snapshotApply(s, good(SourceId::Weather, 0, 21.3f));
  snapshotApply(s, good(SourceId::AirQuality, 0, 3.4f));
  snapshotApply(s, failed(SourceId::AirQuality, 1 * MIN));
  TEST_ASSERT_EQUAL(SourceStatus::Ok,
                    snapshotStatus(s, SourceId::Weather, 2 * MIN));
  TEST_ASSERT_EQUAL(SourceStatus::Failing,
                    snapshotStatus(s, SourceId::AirQuality, 2 * MIN));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_age_is_elapsed_time);
  RUN_TEST(test_age_survives_millis_rollover);
  RUN_TEST(test_empty_snapshot_has_never_seen_anything);
  RUN_TEST(test_good_reading_is_ok_and_keeps_its_value);
  RUN_TEST(test_reading_goes_stale_past_its_threshold);
  RUN_TEST(test_stale_reading_still_carries_its_last_value);
  RUN_TEST(test_thresholds_differ_per_source);
  RUN_TEST(test_sources_age_independently);
  RUN_TEST(test_newer_reading_replaces_older);
  RUN_TEST(test_failure_after_good_reading_is_failing_not_blank);
  RUN_TEST(test_failure_before_any_good_reading_is_still_never_seen);
  RUN_TEST(test_stale_outranks_failing);
  RUN_TEST(test_recovery_clears_the_failure_count);
  RUN_TEST(test_failure_in_one_source_does_not_touch_the_other);
  return UNITY_END();
}
