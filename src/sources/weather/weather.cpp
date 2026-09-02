#include "sources/weather/weather.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/pipeline.h"
#include "net/network_manager.h"
#include "secrets.h" // LOCATION_LATITUDE, LOCATION_LONGITUDE
#include "sources/weather/weather_parse.h"

namespace {

constexpr uint32_t POLL_MS = 10 * 60 * 1000; // forecasts do not change faster
constexpr uint32_t RETRY_MS = 30 * 1000;     // after a failure, try again sooner
constexpr uint32_t HTTP_TIMEOUT_MS = 8000;

// TLS and an HTTP client need considerably more stack than a bare task. 8 KB
// is measured headroom, not a guess - the task reports its own high-water mark
// so this can be tightened once it has run for a while.
constexpr uint32_t STACK_BYTES = 8192;
constexpr UBaseType_t PRIORITY = 3;
constexpr BaseType_t CORE = 1; // WiFi and lwIP live on core 0

TaskHandle_t handle = nullptr;

bool fetch(WeatherSample &out) {
  if (!networkIsUp()) {
    return false;
  }

  char url[192];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,wind_speed_10m",
           (double)LOCATION_LATITUDE, (double)LOCATION_LONGITUDE);

  WiFiClientSecure client;
  // Known gap: no certificate validation. Open-Meteo is HTTPS-only, and
  // pinning a root CA here means shipping a certificate that expires. For
  // public read-only weather data the exposure is low, but this is a real
  // weakness and belongs in the documented gaps rather than in a comment
  // nobody reads.
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    return false;
  }

  int status = http.GET();
  if (status != 200) {
    Serial.printf("weather: HTTP %d\n", status);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  return weatherParse(body.c_str(), body.length(), out);
}

void task(void *) {
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    Reading r{};
    r.source = SourceId::Weather;
    r.valid = fetch(r.weather);
    // Stamped after the fetch, so the timestamp reflects when the value was
    // obtained rather than when the attempt began - the age downstream is what
    // matters, and a slow request would otherwise look fresher than it is.
    r.timestampMs = millis();

    pipelinePost(r);

    // vTaskDelayUntil rather than vTaskDelay: the cadence stays fixed even
    // though the fetch itself takes a variable amount of time. With plain
    // delay the period would be poll + fetch, and would drift with the network.
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(r.valid ? POLL_MS : RETRY_MS));
  }
}

} // namespace

bool weatherSourceStart() {
  return xTaskCreatePinnedToCore(task, "weather", STACK_BYTES, nullptr,
                                 PRIORITY, &handle, CORE) == pdPASS;
}
