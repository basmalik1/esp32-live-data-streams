// On-target tests for the verdict against the live pipeline: the real queue,
// the real fusion task, the real mutex and copy, and - in the second case -
// the real sources with the network pulled out from under them.
//
// The host tests prove the rules. These prove the rules are what the running
// firmware actually applies.

#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>

#include "core/pipeline.h"
#include "core/snapshot.h"
#include "core/verdict.h"
#include "fusion/fusion.h"
#include "net/network_manager.h"
#include "sources/air/air.h"
#include "sources/weather/weather.h"

namespace {

constexpr uint32_t MIN = 60u * 1000u;

bool hasReason(const Verdict &v, Reason r) {
  for (uint8_t i = 0; i < v.reasonCount; i++) {
    if (v.reasons[i] == r) {
      return true;
    }
  }
  return false;
}

// Poll the live snapshot until both sources report the wanted status, or
// give up. Fusion applies readings on its own task, so this is the only
// honest way to wait for it.
bool waitForBoth(SourceStatus want, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    Snapshot s;
    fusionSnapshot(s);
    uint32_t now = millis();
    if (snapshotStatus(s, SourceId::Weather, now) == want &&
        snapshotStatus(s, SourceId::AirQuality, now) == want) {
      return true;
    }
    delay(100);
  }
  return false;
}

Verdict liveVerdict() {
  Snapshot s;
  fusionSnapshot(s);
  return verdictFrom(s, millis());
}

} // namespace

void setUp() {}
void tearDown() {}

// --- TC-5.2 -------------------------------------------------------------------

// A weather reading that is already past its threshold, posted through the
// real queue and folded by the real fusion task. Only its origin is
// synthetic; everything downstream is the live path.
void test_stale_input_degrades_the_live_verdict() {
  TEST_ASSERT_TRUE(pipelineInit(16));
  TEST_ASSERT_TRUE(fusionStart());

  // millis() is small this soon after boot, so run the clock forward in the
  // readings rather than backward: the "old" reading is stamped at 0 and the
  // fresh one at now, and the verdict is asked as of now + threshold.
  uint32_t now = millis() + staleAfterMs(SourceId::Weather) + MIN;

  Reading w{};
  w.source = SourceId::Weather;
  w.timestampMs = 0;
  w.valid = true;
  w.weather.tempC = -10.0f; // would band Poor if it were allowed to count
  w.weather.windKph = 5.0f;
  TEST_ASSERT_TRUE(pipelinePost(w));

  Reading a{};
  a.source = SourceId::AirQuality;
  a.timestampMs = now - MIN;
  a.valid = true;
  a.air.aqi = 20;
  TEST_ASSERT_TRUE(pipelinePost(a));

  delay(200); // fusion runs at a lower priority than this test task; yield

  Snapshot s;
  fusionSnapshot(s);
  TEST_ASSERT_EQUAL(SourceStatus::Stale,
                    snapshotStatus(s, SourceId::Weather, now));
  TEST_ASSERT_EQUAL(SourceStatus::Ok,
                    snapshotStatus(s, SourceId::AirQuality, now));

  Verdict v = verdictFrom(s, now);
  TEST_ASSERT_EQUAL(Outcome::Good, v.outcome); // the cold reading was excluded
  TEST_ASSERT_EQUAL(Confidence::Low, v.confidence);
  TEST_ASSERT_TRUE(hasReason(v, Reason::WeatherStale));
  TEST_ASSERT_FALSE(hasReason(v, Reason::Cold));
}

// --- TC-5.3 -------------------------------------------------------------------

// The system-level case: everything real. Join, let both sources arrive,
// confirm a confident verdict, pull the network, force a poll, and check that
// the verdict hedges instead of repeating itself.
void test_network_removed_mid_run_makes_the_verdict_hedge() {
  TEST_ASSERT_TRUE_MESSAGE(networkConnect(), "wifi join failed");
  TEST_ASSERT_TRUE(weatherSourceStart());
  TEST_ASSERT_TRUE(airSourceStart());

  TEST_ASSERT_TRUE_MESSAGE(waitForBoth(SourceStatus::Ok, 40000),
                           "both sources did not arrive within 40 s");
  Verdict before = liveVerdict();
  TEST_ASSERT_EQUAL(Confidence::High, before.confidence);
  TEST_ASSERT_NOT_EQUAL(Outcome::Unknown, before.outcome);
  Serial.printf("TC-5.3: before  %s / %s\n", outcomeName(before.outcome),
                confidenceName(before.confidence));

  // Pull the network. The sources will not notice until they next fetch,
  // which is minutes away - so tell them to fetch now.
  WiFi.disconnect(true);
  delay(500);
  TEST_ASSERT_FALSE(networkIsUp());
  weatherSourcePollNow();
  airSourcePollNow();

  TEST_ASSERT_TRUE_MESSAGE(waitForBoth(SourceStatus::Failing, 15000),
                           "sources did not report failure within 15 s");
  Verdict after = liveVerdict();
  Serial.printf("TC-5.3: after   %s / %s\n", outcomeName(after.outcome),
                confidenceName(after.confidence));

  // The last good values are still fresh, so the outcome stands - but the
  // confidence must not.
  TEST_ASSERT_EQUAL(before.outcome, after.outcome);
  TEST_ASSERT_EQUAL(Confidence::Low, after.confidence);
  TEST_ASSERT_TRUE(hasReason(after, Reason::WeatherFailing));
  TEST_ASSERT_TRUE(hasReason(after, Reason::AirFailing));

  // One forced poll is one fetch. The first version of PollNow() left the
  // cadence's wake time in the future, and the next wait refused to block, so
  // every forced poll fetched twice. Give the retry no time to fire, and
  // count.
  delay(2000);
  Snapshot s;
  fusionSnapshot(s);
  TEST_ASSERT_EQUAL_UINT16(
      1, s.source[(size_t)SourceId::Weather].consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT16(
      1, s.source[(size_t)SourceId::AirQuality].consecutiveFailures);
  // What happens after the thresholds - Unknown - is the host case
  // test_everything_stale_is_unknown; it takes 45 minutes here.
}

void setup() {
  delay(3000); // let the test runner reopen the port after the upload reset
  UNITY_BEGIN();
  RUN_TEST(test_stale_input_degrades_the_live_verdict);
  RUN_TEST(test_network_removed_mid_run_makes_the_verdict_hedge);
  UNITY_END();
}

void loop() {}
