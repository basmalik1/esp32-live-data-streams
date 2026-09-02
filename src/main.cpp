#include <Arduino.h>

#include "core/pipeline.h"
#include "net/network_manager.h"
#include "sinks/serial/serial_sink.h"
#include "sources/weather/weather.h"

// Depth chosen so a sink stalled for a few minutes still loses nothing: at one
// reading per source per 10 minutes, 16 slots is well over an hour of history.
// It will need revisiting when the push stream lands and starts producing
// faster than anything drains it - which is the point of that milestone.
constexpr uint32_t QUEUE_DEPTH = 16;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  Serial.println();
  Serial.println("esp32-live-data-streams");

  if (!pipelineInit(QUEUE_DEPTH)) {
    Serial.println("fatal: could not create the pipeline");
    return;
  }

  // The sink starts first so that anything a source posts during startup is
  // drained rather than sitting in the queue until the first idle tick.
  if (!serialSinkStart()) {
    Serial.println("fatal: could not start the sink");
    return;
  }

  // A failed join is not fatal. The source will post failure readings and
  // retry, which is exactly the behaviour project 3 needs when a sensor is
  // disconnected - the system reports degraded rather than stopping.
  networkConnect();

  if (!weatherSourceStart()) {
    Serial.println("fatal: could not start the weather source");
  }
}

void loop() {
  // Deliberately empty. Every unit of work lives in a task with its own stack,
  // priority and cadence; the Arduino loop task simply has nothing to do.
  // Suspending it hands its slice back to the scheduler.
  vTaskDelay(portMAX_DELAY);
}
