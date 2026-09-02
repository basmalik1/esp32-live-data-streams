// Parser tests for the Open-Meteo forecast response.
//
// Runs on the host against captured fixtures. Every case here is a real thing
// an HTTP response can be - truncated by a dropped connection, missing a field
// the service had no data for, or carrying a type the parser did not expect.
// None of them need a board, and none of them are convenient to reproduce on
// one.

#include <cstring>
#include <unity.h>

#include "sources/weather/weather_parse.h"

// Captured from api.open-meteo.com/v1/forecast, trimmed to the fields used.
static const char *REAL_RESPONSE = R"({
  "latitude": 51.5,
  "longitude": -0.12,
  "generationtime_ms": 0.0412,
  "utc_offset_seconds": 0,
  "timezone": "GMT",
  "elevation": 23.0,
  "current_units": {"time":"iso8601","temperature_2m":"°C"},
  "current": {
    "time": "2026-08-20T12:00",
    "interval": 900,
    "temperature_2m": 21.3,
    "relative_humidity_2m": 64,
    "wind_speed_10m": 11.2
  }
})";

void setUp() {}
void tearDown() {}

void test_parses_a_real_response() {
  WeatherSample s{};
  TEST_ASSERT_TRUE(weatherParse(REAL_RESPONSE, strlen(REAL_RESPONSE), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.3f, s.tempC);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 64.0f, s.humidityPct);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.2f, s.windKph);
}

// Humidity arrives as a JSON integer, not a float. A parser that demanded a
// float type would reject a perfectly good response.
void test_accepts_integer_valued_fields() {
  const char *j = R"({"current":{"temperature_2m":21,"relative_humidity_2m":64,"wind_speed_10m":11}})";
  WeatherSample s{};
  TEST_ASSERT_TRUE(weatherParse(j, strlen(j), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, s.tempC);
}

void test_rejects_malformed_json() {
  const char *j = R"({"current": {"temperature_2m": )";
  WeatherSample s{};
  TEST_ASSERT_FALSE(weatherParse(j, strlen(j), s));
}

// A connection dropped mid-body gives valid-looking JSON that simply stops.
void test_rejects_truncated_response() {
  char truncated[64];
  memcpy(truncated, REAL_RESPONSE, sizeof(truncated) - 1);
  truncated[sizeof(truncated) - 1] = '\0';
  WeatherSample s{};
  TEST_ASSERT_FALSE(weatherParse(truncated, strlen(truncated), s));
}

void test_rejects_response_with_no_current_block() {
  const char *j = R"({"latitude":51.5,"longitude":-0.12})";
  WeatherSample s{};
  TEST_ASSERT_FALSE(weatherParse(j, strlen(j), s));
}

// The failure that matters most: the service omits a key it has no data for.
// A parser that let this through would report 0.0 C, which is indistinguishable
// downstream from a real freezing measurement.
void test_rejects_missing_field_rather_than_defaulting_to_zero() {
  const char *j = R"({"current":{"temperature_2m":21.3,"wind_speed_10m":11.2}})";
  WeatherSample s{};
  s.humidityPct = -1.0f;
  TEST_ASSERT_FALSE(weatherParse(j, strlen(j), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, s.humidityPct); // left untouched
}

void test_rejects_wrong_type() {
  const char *j = R"({"current":{"temperature_2m":"warm","relative_humidity_2m":64,"wind_speed_10m":11}})";
  WeatherSample s{};
  TEST_ASSERT_FALSE(weatherParse(j, strlen(j), s));
}

void test_rejects_empty_input() {
  WeatherSample s{};
  TEST_ASSERT_FALSE(weatherParse(nullptr, 0, s));
  TEST_ASSERT_FALSE(weatherParse("", 0, s));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_real_response);
  RUN_TEST(test_accepts_integer_valued_fields);
  RUN_TEST(test_rejects_malformed_json);
  RUN_TEST(test_rejects_truncated_response);
  RUN_TEST(test_rejects_response_with_no_current_block);
  RUN_TEST(test_rejects_missing_field_rather_than_defaulting_to_zero);
  RUN_TEST(test_rejects_wrong_type);
  RUN_TEST(test_rejects_empty_input);
  return UNITY_END();
}
