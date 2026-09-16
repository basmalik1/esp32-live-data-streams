// Parser tests for the Open-Meteo air-quality response.
//
// Same shape as the weather parser tests, and for the same reason: every
// case is something an HTTP response can actually be, and none of them is
// convenient to produce against a live service.

#include <cstring>
#include <unity.h>

#include "sources/air/air_parse.h"

// Captured from air-quality-api.open-meteo.com/v1/air-quality.
static const char *REAL_RESPONSE = R"({
  "latitude": 51.5,
  "longitude": -0.10000038,
  "generationtime_ms": 0.527501106262207,
  "utc_offset_seconds": 0,
  "timezone": "GMT",
  "timezone_abbreviation": "GMT",
  "elevation": 16.0,
  "current_units": {"time":"iso8601","interval":"seconds","pm2_5":"μg/m³","pm10":"μg/m³","us_aqi":"USAQI"},
  "current": {
    "time": "2026-09-16T01:00",
    "interval": 3600,
    "pm2_5": 3.4,
    "pm10": 6.3,
    "us_aqi": 26
  }
})";

void setUp() {}
void tearDown() {}

void test_parses_a_real_response() {
  AirSample s{};
  TEST_ASSERT_TRUE(airParse(REAL_RESPONSE, strlen(REAL_RESPONSE), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.4f, s.pm25);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.3f, s.pm10);
  TEST_ASSERT_EQUAL_INT16(26, s.aqi);
}

// Clean air rounds to whole numbers, so the particulate fields can arrive as
// integers. A parser that demanded a float would reject a good response.
void test_accepts_integer_valued_particulates() {
  const char *j = R"({"current":{"pm2_5":3,"pm10":6,"us_aqi":26}})";
  AirSample s{};
  TEST_ASSERT_TRUE(airParse(j, strlen(j), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, s.pm25);
}

// The index is an integer by definition. A fractional one is not a value this
// service produces, so it is a sign the response is not what we think it is.
void test_rejects_fractional_aqi() {
  const char *j = R"({"current":{"pm2_5":3.4,"pm10":6.3,"us_aqi":26.5}})";
  AirSample s{};
  TEST_ASSERT_FALSE(airParse(j, strlen(j), s));
}

void test_rejects_malformed_json() {
  const char *j = R"({"current": {"pm2_5": )";
  AirSample s{};
  TEST_ASSERT_FALSE(airParse(j, strlen(j), s));
}

void test_rejects_truncated_response() {
  char truncated[64];
  memcpy(truncated, REAL_RESPONSE, sizeof(truncated) - 1);
  truncated[sizeof(truncated) - 1] = '\0';
  AirSample s{};
  TEST_ASSERT_FALSE(airParse(truncated, strlen(truncated), s));
}

void test_rejects_response_with_no_current_block() {
  const char *j = R"({"latitude":51.5,"longitude":-0.1})";
  AirSample s{};
  TEST_ASSERT_FALSE(airParse(j, strlen(j), s));
}

// 0 µg/m³ is a real and excellent reading, which is exactly why a missing
// field must not be allowed to default to it.
void test_rejects_missing_field_rather_than_defaulting_to_zero() {
  const char *j = R"({"current":{"pm2_5":3.4,"us_aqi":26}})";
  AirSample s{};
  s.pm10 = -1.0f;
  TEST_ASSERT_FALSE(airParse(j, strlen(j), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, s.pm10); // left untouched
}

void test_rejects_wrong_type() {
  const char *j = R"({"current":{"pm2_5":"low","pm10":6.3,"us_aqi":26}})";
  AirSample s{};
  TEST_ASSERT_FALSE(airParse(j, strlen(j), s));
}

void test_rejects_empty_input() {
  AirSample s{};
  TEST_ASSERT_FALSE(airParse(nullptr, 0, s));
  TEST_ASSERT_FALSE(airParse("", 0, s));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_real_response);
  RUN_TEST(test_accepts_integer_valued_particulates);
  RUN_TEST(test_rejects_fractional_aqi);
  RUN_TEST(test_rejects_malformed_json);
  RUN_TEST(test_rejects_truncated_response);
  RUN_TEST(test_rejects_response_with_no_current_block);
  RUN_TEST(test_rejects_missing_field_rather_than_defaulting_to_zero);
  RUN_TEST(test_rejects_wrong_type);
  RUN_TEST(test_rejects_empty_input);
  return UNITY_END();
}
