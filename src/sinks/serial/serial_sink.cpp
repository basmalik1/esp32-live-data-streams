#include "sinks/serial/serial_sink.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/pipeline.h"

namespace {

constexpr uint32_t STACK_BYTES = 4096;
constexpr UBaseType_t PRIORITY = 2; // below the sources: producers come first
constexpr BaseType_t CORE = 1;
constexpr uint32_t TAKE_TIMEOUT_MS = 5000;

TaskHandle_t handle = nullptr;

void print(const Reading &r) {
  uint32_t ageMs = millis() - r.timestampMs;

  if (!r.valid) {
    Serial.printf("[%7lu] %-8s FAILED\n", r.timestampMs, sourceName(r.source));
    return;
  }

  switch (r.source) {
  case SourceId::Weather:
    Serial.printf("[%7lu] %-8s %.1f C  %.0f%% RH  %.1f km/h   (age %lu ms)\n",
                  r.timestampMs, sourceName(r.source), r.weather.tempC,
                  r.weather.humidityPct, r.weather.windKph, ageMs);
    break;
  case SourceId::AirQuality:
    Serial.printf("[%7lu] %-8s pm2.5 %.1f  aqi %d   (age %lu ms)\n",
                  r.timestampMs, sourceName(r.source), r.air.pm25, r.air.aqi,
                  ageMs);
    break;
  default:
    break;
  }
}

void task(void *) {
  for (;;) {
    Reading r{};
    if (pipelineTake(r, TAKE_TIMEOUT_MS)) {
      print(r);
      continue;
    }

    // Nothing arrived within the timeout. Worth saying so rather than sitting
    // silent: "no output" is indistinguishable from a crash, and a heartbeat
    // that also carries queue depth and stack headroom costs one line.
    Serial.printf("[%7lu] idle     queued=%lu dropped=%lu stack_free=%u\n",
                  millis(), pipelineQueued(), pipelineDropped(),
                  uxTaskGetStackHighWaterMark(nullptr));
  }
}

} // namespace

bool serialSinkStart() {
  return xTaskCreatePinnedToCore(task, "serial_sink", STACK_BYTES, nullptr,
                                 PRIORITY, &handle, CORE) == pdPASS;
}
