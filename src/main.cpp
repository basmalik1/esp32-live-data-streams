#include <Arduino.h>

#include "core/pipeline.h"
#include "fusion/fusion.h"
#include "net/network_manager.h"
#include "sinks/serial/serial_sink.h"
#include "sources/air/air.h"
#include "sources/weather/weather.h"

// Depth chosen so a stalled consumer still loses nothing for a long while: two
// polled sources produce a reading every few minutes each, so 16 slots is well
// over half an hour of history. It will need revisiting when the push stream
// lands and starts producing faster than anything drains it - which is the
// point of that milestone.
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

  // The consumer starts first so that anything a source posts during startup
  // is drained rather than sitting in the queue until the first idle tick.
  if (!fusionStart()) {
    Serial.println("fatal: could not start the fusion task");
    return;
  }
  if (!serialSinkStart()) {
    Serial.println("fatal: could not start the sink");
    return;
  }

  // A failed join is not fatal. The sources post failure readings and retry,
  // so the system reports degraded rather than stopping - which is what any
  // consumer downstream needs in order to hedge rather than guess.
  networkConnect();

  if (!weatherSourceStart()) {
    Serial.println("fatal: could not start the weather source");
  }
  if (!airSourceStart()) {
    Serial.println("fatal: could not start the air source");
  }
}

void loop() {
  // Deliberately empty. Every unit of work lives in a task with its own stack,
  // priority and cadence; the Arduino loop task simply has nothing to do.
  // Suspending it hands its slice back to the scheduler.
  vTaskDelay(portMAX_DELAY);
}
