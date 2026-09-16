#include "sources/weather/weather.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/pipeline.h"
#include "net/https_get.h"
#include "secrets.h" // LOCATION_LATITUDE, LOCATION_LONGITUDE
#include "sources/weather/weather_parse.h"

namespace {

constexpr uint32_t POLL_MS = 10 * 60 * 1000; // forecasts do not change faster
constexpr uint32_t RETRY_MS = 30 * 1000;     // after a failure, try again sooner

// TLS and an HTTP client need considerably more stack than a bare task. 8 KB
// is a guess with headroom; the task prints its own high-water mark after
// every fetch so the guess can be replaced by a measurement.
constexpr uint32_t STACK_BYTES = 8192;
constexpr UBaseType_t PRIORITY = 3;
constexpr BaseType_t CORE = 1; // WiFi and lwIP live on core 0

TaskHandle_t handle = nullptr;

bool fetch(WeatherSample &out) {
  char url[192];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,wind_speed_10m",
           (double)LOCATION_LATITUDE, (double)LOCATION_LONGITUDE);

  String body;
  if (!httpsGet(url, "weather", body)) {
    return false;
  }
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

    // The high-water mark is lowest right after the TLS handshake, which is
    // the only moment this number means anything.
    Serial.printf("weather: stack_free=%u\n", uxTaskGetStackHighWaterMark(nullptr));

    // vTaskDelayUntil rather than vTaskDelay: the cadence stays fixed even
    // though the fetch itself takes a variable amount of time. With plain
    // delay the period would be poll + fetch, and would drift with the network.
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(r.valid ? POLL_MS : RETRY_MS));

    // Back before the wake time we were given means PollNow() cut the wait
    // short. Restart the cadence from this forced poll. Leaving `wake` in the
    // future is not harmless: the next vTaskDelayUntil would see a wake time
    // ahead of the clock, decide no delay is needed, and fetch a second time
    // immediately. (xTaskDelayUntil's return value does not report an abort -
    // it reports whether any delay happened - so the clock comparison is the
    // only reliable signal.)
    if ((int32_t)(xTaskGetTickCount() - wake) < 0) {
      wake = xTaskGetTickCount();
    }
  }
}

} // namespace

bool weatherSourceStart() {
  return xTaskCreatePinnedToCore(task, "weather", STACK_BYTES, nullptr,
                                 PRIORITY, &handle, CORE) == pdPASS;
}

void weatherSourcePollNow() {
  if (handle != nullptr) {
    xTaskAbortDelay(handle);
  }
}
