#include "sources/air/air_parse.h"

#include <ArduinoJson.h>

bool airParse(const char *json, size_t len, AirSample &out) {
  if (json == nullptr || len == 0) {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
    return false;
  }

  JsonObjectConst current = doc["current"];
  if (current.isNull()) {
    return false;
  }

  JsonVariantConst pm25 = current["pm2_5"];
  JsonVariantConst pm10 = current["pm10"];
  JsonVariantConst aqi = current["us_aqi"];

  // The AQI is an integer index, so it must arrive as one. The particulate
  // fields are accepted as either integer or float, as the weather fields are.
  if (!pm25.is<float>() || !pm10.is<float>() || !aqi.is<int>()) {
    return false;
  }

  out.pm25 = pm25.as<float>();
  out.pm10 = pm10.as<float>();
  out.aqi = (int16_t)aqi.as<int>();
  return true;
}
