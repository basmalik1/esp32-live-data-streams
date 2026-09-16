// On-target tests for the source tasks: a failure is posted, not swallowed,
// and an HTTP error is a failure rather than silence.
//
// The first case runs before the network is joined - which is the cheapest
// way to make a fetch fail on real hardware, and exactly what happens at
// every boot until the join completes.

#include <Arduino.h>
#include <unity.h>

#include "core/pipeline.h"
#include "net/https_get.h"
#include "net/network_manager.h"
#include "sources/weather/weather.h"

void setUp() {}
void tearDown() {}

// --- TC-3.1 -------------------------------------------------------------------

void test_failed_fetch_posts_an_invalid_reading_not_nothing() {
  TEST_ASSERT_TRUE(pipelineInit(16));
  TEST_ASSERT_FALSE(networkIsUp()); // not joined yet, on purpose

  TEST_ASSERT_TRUE(weatherSourceStart());

  Reading r{};
  TEST_ASSERT_TRUE_MESSAGE(pipelineTake(r, 5000),
                           "no reading arrived - the failure was swallowed");
  TEST_ASSERT_EQUAL(SourceId::Weather, r.source);
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_TRUE(r.timestampMs > 0);
}

// --- TC-3.2 -------------------------------------------------------------------

void test_http_error_is_reported_as_failure() {
  TEST_ASSERT_TRUE_MESSAGE(networkConnect(), "wifi join failed");

  String body;
  // A real endpoint succeeds, so the failure below is the URL's fault.
  TEST_ASSERT_TRUE(httpsGet("https://api.open-meteo.com/v1/forecast?latitude=0"
                            "&longitude=0&current=temperature_2m",
                            "probe", body));
  TEST_ASSERT_TRUE(body.length() > 0);

  body = "";
  TEST_ASSERT_FALSE(httpsGet("https://api.open-meteo.com/v1/does-not-exist",
                             "probe", body));
  TEST_ASSERT_EQUAL_UINT32(0, body.length()); // nothing left behind
}

void setup() {
  delay(3000); // let the test runner reopen the port after the upload reset
  UNITY_BEGIN();
  RUN_TEST(test_failed_fetch_posts_an_invalid_reading_not_nothing);
  RUN_TEST(test_http_error_is_reported_as_failure);
  UNITY_END();
}

void loop() {}
