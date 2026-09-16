#include "sinks/serial/serial_sink.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/pipeline.h"
#include "core/snapshot.h"
#include "fusion/fusion.h"

namespace {

constexpr uint32_t STACK_BYTES = 4096;
constexpr UBaseType_t PRIORITY = 1; // below fusion, which is below the sources
constexpr BaseType_t CORE = 1;
constexpr uint32_t PERIOD_MS = 10000;

TaskHandle_t handle = nullptr;

void printSource(const Snapshot &snap, SourceId id, uint32_t nowMs) {
  const SourceState &st = snap.source[(size_t)id];
  SourceStatus status = snapshotStatus(snap, id, nowMs);

  Serial.printf("  %-8s %-8s", sourceName(id), statusName(status));

  if (!st.everSucceeded) {
    Serial.printf("  (%u failed)\n", st.consecutiveFailures);
    return;
  }

  uint32_t ageS = readingAgeMs(nowMs, st.lastGood.timestampMs) / 1000;
  switch (id) {
  case SourceId::Weather:
    Serial.printf("  %.1f C  %.0f%% RH  %.1f km/h", st.lastGood.weather.tempC,
                  st.lastGood.weather.humidityPct, st.lastGood.weather.windKph);
    break;
  case SourceId::AirQuality:
    Serial.printf("  pm2.5 %.1f  pm10 %.1f  aqi %d", st.lastGood.air.pm25,
                  st.lastGood.air.pm10, st.lastGood.air.aqi);
    break;
  default:
    break;
  }
  Serial.printf("  age %lus", ageS);
  if (st.consecutiveFailures > 0) {
    Serial.printf("  (%u failed since)", st.consecutiveFailures);
  }
  Serial.println();
}

void task(void *) {
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));

    Snapshot snap;
    fusionSnapshot(snap);
    uint32_t now = millis();

    Serial.printf("[%7lu] snapshot queued=%lu dropped=%lu stack_free=%u\n", now,
                  pipelineQueued(), pipelineDropped(),
                  uxTaskGetStackHighWaterMark(nullptr));
    for (size_t i = 0; i < (size_t)SourceId::Count; i++) {
      printSource(snap, (SourceId)i, now);
    }
  }
}

} // namespace

bool serialSinkStart() {
  return xTaskCreatePinnedToCore(task, "serial_sink", STACK_BYTES, nullptr,
                                 PRIORITY, &handle, CORE) == pdPASS;
}
