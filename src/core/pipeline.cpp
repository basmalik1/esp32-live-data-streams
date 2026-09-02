#include "core/pipeline.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace {

QueueHandle_t queue = nullptr;
SemaphoreHandle_t overflowLock = nullptr;
volatile uint32_t dropped = 0;

} // namespace

const char *sourceName(SourceId id) {
  switch (id) {
  case SourceId::Weather:
    return "weather";
  case SourceId::AirQuality:
    return "air";
  default:
    return "?";
  }
}

bool pipelineInit(uint32_t depth) {
  queue = xQueueCreate(depth, sizeof(Reading));
  overflowLock = xSemaphoreCreateMutex();
  return queue != nullptr && overflowLock != nullptr;
}

bool pipelinePost(const Reading &r) {
  if (queue == nullptr) {
    return false;
  }

  // Fast path: room available, no lock taken. This is the common case and it
  // stays lock-free so producers do not serialise against each other.
  if (xQueueSend(queue, &r, 0) == pdTRUE) {
    return true;
  }

  // Slow path: the queue is full, so make room. Discarding and re-sending is
  // two operations, and without a lock two producers arriving together could
  // each discard one - costing two readings to admit two, instead of one.
  // The lock is held for the duration of two non-blocking queue calls.
  if (xSemaphoreTake(overflowLock, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false; // rather than block a real-time producer
  }

  Reading discard;
  if (xQueueReceive(queue, &discard, 0) == pdTRUE) {
    dropped++;
  }
  bool ok = xQueueSend(queue, &r, 0) == pdTRUE;

  xSemaphoreGive(overflowLock);
  return ok;
}

bool pipelineTake(Reading &out, uint32_t timeoutMs) {
  if (queue == nullptr) {
    return false;
  }
  return xQueueReceive(queue, &out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

uint32_t pipelineDropped() { return dropped; }

uint32_t pipelineQueued() {
  return queue == nullptr ? 0 : uxQueueMessagesWaiting(queue);
}
