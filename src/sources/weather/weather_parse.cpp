#include "sources/weather/weather_parse.h"

#include <ArduinoJson.h>

bool weatherParse(const char *json, size_t len, WeatherSample &out) {
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

  // Every field is checked before any is used. Open-Meteo omits keys it has no
  // data for rather than sending nulls, so a missing key is the realistic
  // failure - and ArduinoJson would hand back 0.0 for it without complaint.
  JsonVariantConst temp = current["temperature_2m"];
  JsonVariantConst humidity = current["relative_humidity_2m"];
  JsonVariantConst wind = current["wind_speed_10m"];

  if (!temp.is<float>() || !humidity.is<float>() || !wind.is<float>()) {
    return false;
  }

  out.tempC = temp.as<float>();
  out.humidityPct = humidity.as<float>();
  out.windKph = wind.as<float>();
  return true;
}
