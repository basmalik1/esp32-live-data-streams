#include "fusion/fusion.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "core/pipeline.h"

namespace {

constexpr uint32_t STACK_BYTES = 4096;
constexpr UBaseType_t PRIORITY = 2; // below the sources: producers come first
constexpr BaseType_t CORE = 1;
constexpr uint32_t TAKE_TIMEOUT_MS = 5000;

TaskHandle_t handle = nullptr;
SemaphoreHandle_t lock = nullptr;
Snapshot snapshot{};

// One line per arrival. This is the observable for "a reading posted by a
// source reached the consumer", so it stays even though the sink prints a
// fuller picture on its own schedule.
void logArrival(const Reading &r) {
  if (!r.valid) {
    Serial.printf("[%7lu] %-8s FAILED\n", r.timestampMs, sourceName(r.source));
    return;
  }
  switch (r.source) {
  case SourceId::Weather:
    Serial.printf("[%7lu] %-8s %.1f C  %.0f%% RH  %.1f km/h\n", r.timestampMs,
                  sourceName(r.source), r.weather.tempC, r.weather.humidityPct,
                  r.weather.windKph);
    break;
  case SourceId::AirQuality:
    Serial.printf("[%7lu] %-8s pm2.5 %.1f  pm10 %.1f  aqi %d\n", r.timestampMs,
                  sourceName(r.source), r.air.pm25, r.air.pm10, r.air.aqi);
    break;
  default:
    break;
  }
}

void task(void *) {
  for (;;) {
    Reading r{};
    if (!pipelineTake(r, TAKE_TIMEOUT_MS)) {
      // Nothing arrived. Saying so is cheaper than the alternative: "no
      // output" and "the firmware died" look identical on a serial monitor.
      Serial.printf("[%7lu] idle     queued=%lu dropped=%lu stack_free=%u\n",
                    millis(), pipelineQueued(), pipelineDropped(),
                    uxTaskGetStackHighWaterMark(nullptr));
      continue;
    }

    // The lock covers a struct update and nothing else. snapshotApply has no
    // clock and no I/O, so the hold time is a few dozen instructions.
    xSemaphoreTake(lock, portMAX_DELAY);
    snapshotApply(snapshot, r);
    xSemaphoreGive(lock);

    logArrival(r);
  }
}

} // namespace

bool fusionStart() {
  lock = xSemaphoreCreateMutex();
  if (lock == nullptr) {
    return false;
  }
  return xTaskCreatePinnedToCore(task, "fusion", STACK_BYTES, nullptr, PRIORITY,
                                 &handle, CORE) == pdPASS;
}

void fusionSnapshot(Snapshot &out) {
  if (lock == nullptr) {
    out = Snapshot{};
    return;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  out = snapshot;
  xSemaphoreGive(lock);
}
