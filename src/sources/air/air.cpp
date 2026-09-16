#include "sources/air/air.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/pipeline.h"
#include "net/https_get.h"
#include "secrets.h" // LOCATION_LATITUDE, LOCATION_LONGITUDE
#include "sources/air/air_parse.h"

namespace {

// The service itself only updates hourly, so polling faster than this buys
// nothing. Fifteen minutes rather than ten also means the two sources are
// genuinely on different schedules, which is what fusion exists to handle.
constexpr uint32_t POLL_MS = 15 * 60 * 1000;
constexpr uint32_t RETRY_MS = 30 * 1000;

constexpr uint32_t STACK_BYTES = 8192; // same TLS path as weather, same guess
constexpr UBaseType_t PRIORITY = 3;
constexpr BaseType_t CORE = 1;

TaskHandle_t handle = nullptr;

bool fetch(AirSample &out) {
  char url[192];
  snprintf(url, sizeof(url),
           "https://air-quality-api.open-meteo.com/v1/air-quality"
           "?latitude=%.4f&longitude=%.4f&current=pm2_5,pm10,us_aqi",
           (double)LOCATION_LATITUDE, (double)LOCATION_LONGITUDE);

  String body;
  if (!httpsGet(url, "air", body)) {
    return false;
  }
  return airParse(body.c_str(), body.length(), out);
}

void task(void *) {
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    Reading r{};
    r.source = SourceId::AirQuality;
    r.valid = fetch(r.air);
    r.timestampMs = millis(); // after the fetch, as with weather

    pipelinePost(r);

    Serial.printf("air: stack_free=%u\n", uxTaskGetStackHighWaterMark(nullptr));

    vTaskDelayUntil(&wake, pdMS_TO_TICKS(r.valid ? POLL_MS : RETRY_MS));
  }
}

} // namespace

bool airSourceStart() {
  return xTaskCreatePinnedToCore(task, "air", STACK_BYTES, nullptr, PRIORITY,
                                 &handle, CORE) == pdPASS;
}
