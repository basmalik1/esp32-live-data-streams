// On-target tests for the queue: overflow policy and the producer's promise
// never to block.
//
// These run on the board because the questions are about a real FreeRTOS
// queue under real scheduling. A host mock of the scheduler would be a second
// implementation with its own bugs.

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include "core/pipeline.h"

namespace {

Reading weatherAt(uint32_t ts) {
  Reading r{};
  r.source = SourceId::Weather;
  r.timestampMs = ts;
  r.valid = true;
  r.weather.tempC = (float)ts;
  return r;
}

// --- TC-4.2 fixtures ---------------------------------------------------------

constexpr int PRODUCERS = 2;
constexpr int POSTS_PER_PRODUCER = 500;
constexpr uint32_t CONSUMER_SLEEP_MS = 2;

struct ProducerStats {
  int posted;
  int64_t maxPostUs;
  volatile bool done;
};

ProducerStats stats[PRODUCERS];
volatile int taken = 0;
volatile bool stopConsumer = false;

void producer(void *arg) {
  ProducerStats &st = *(ProducerStats *)arg;
  for (int i = 0; i < POSTS_PER_PRODUCER; i++) {
    int64_t t0 = esp_timer_get_time();
    pipelinePost(weatherAt((uint32_t)i));
    int64_t dt = esp_timer_get_time() - t0;
    if (dt > st.maxPostUs) {
      st.maxPostUs = dt;
    }
    st.posted++;
  }
  st.done = true;
  vTaskDelete(nullptr);
}

// Deliberately slower than the producers. The point is to saturate the queue.
void consumer(void *) {
  Reading r;
  while (!stopConsumer) {
    if (pipelineTake(r, 0)) {
      taken = taken + 1;
    }
    vTaskDelay(pdMS_TO_TICKS(CONSUMER_SLEEP_MS));
  }
  vTaskDelete(nullptr);
}

} // namespace

void setUp() {}
void tearDown() {}

// --- TC-4.1 -------------------------------------------------------------------

void test_full_queue_drops_oldest_and_counts_it() {
  TEST_ASSERT_TRUE(pipelineInit(4));

  for (uint32_t i = 1; i <= 6; i++) {
    TEST_ASSERT_TRUE(pipelinePost(weatherAt(i)));
  }

  TEST_ASSERT_EQUAL_UINT32(2, pipelineDropped());
  TEST_ASSERT_EQUAL_UINT32(4, pipelineQueued());

  // What remains is the newest four, in order. 1 and 2 are gone.
  for (uint32_t expect = 3; expect <= 6; expect++) {
    Reading r{};
    TEST_ASSERT_TRUE(pipelineTake(r, 0));
    TEST_ASSERT_EQUAL_UINT32(expect, r.timestampMs);
  }
  Reading none{};
  TEST_ASSERT_FALSE(pipelineTake(none, 0));
}

// --- TC-4.2 -------------------------------------------------------------------

void test_producers_never_block_against_a_slow_consumer() {
  // Fresh, small queue so it saturates immediately. pipelineInit creates a
  // new queue; the old one from TC-4.1 is simply abandoned.
  TEST_ASSERT_TRUE(pipelineInit(8));
  uint32_t dropsBefore = pipelineDropped();

  taken = 0;
  stopConsumer = false;
  for (auto &st : stats) {
    st = ProducerStats{};
  }

  xTaskCreatePinnedToCore(consumer, "consumer", 4096, nullptr, 1, nullptr, 1);
  for (int i = 0; i < PRODUCERS; i++) {
    xTaskCreatePinnedToCore(producer, "producer", 4096, &stats[i], 3, nullptr,
                            1);
  }

  uint32_t start = millis();
  bool allDone = false;
  while (!allDone && millis() - start < 10000) {
    allDone = true;
    for (auto &st : stats) {
      allDone = allDone && st.done;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  TEST_ASSERT_TRUE_MESSAGE(allDone, "producers did not finish in 10 s");

  // Let the consumer drain what is left, then stop it.
  vTaskDelay(pdMS_TO_TICKS(100));
  stopConsumer = true;
  vTaskDelay(pdMS_TO_TICKS(20));

  int posted = 0;
  int64_t maxPostUs = 0;
  for (auto &st : stats) {
    posted += st.posted;
    if (st.maxPostUs > maxPostUs) {
      maxPostUs = st.maxPostUs;
    }
  }
  uint32_t dropped = pipelineDropped() - dropsBefore;

  Serial.printf("TC-4.2: posted=%d taken=%d queued=%lu dropped=%lu "
                "max_post_us=%lld\n",
                posted, taken, pipelineQueued(), dropped, maxPostUs);

  TEST_ASSERT_EQUAL_INT(PRODUCERS * POSTS_PER_PRODUCER, posted);
  // Every reading is accounted for: taken, still queued, or counted as dropped.
  TEST_ASSERT_EQUAL_INT(posted, taken + (int)pipelineQueued() + (int)dropped);
  // The consumer was slower, so the queue must have overflowed.
  TEST_ASSERT_TRUE_MESSAGE(dropped > 0, "queue never saturated; test is moot");
  // The promise: no post ever waited. The overflow lock times out at 10 ms,
  // so a producer that blocked would show as a post in the thousands of us.
  TEST_ASSERT_TRUE_MESSAGE(maxPostUs < 1000, "a post took over 1 ms");
}

void setup() {
  delay(3000); // let the test runner reopen the port after the upload reset
  UNITY_BEGIN();
  RUN_TEST(test_full_queue_drops_oldest_and_counts_it);
  RUN_TEST(test_producers_never_block_against_a_slow_consumer);
  UNITY_END();
}

void loop() {}
