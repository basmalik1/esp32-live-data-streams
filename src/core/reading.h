#pragma once

#include <stdint.h>

// Everything that flows through the pipeline, whatever produced it.
//
// Sources never talk to sinks. They post Readings to a queue and the fusion
// stage decides what they mean, which is what lets an HTTP poller today be
// replaced by an I2C or CAN driver later without anything downstream changing.
enum class SourceId : uint8_t {
  Weather = 0,
  AirQuality,
  Count,
};

const char *sourceName(SourceId id);

struct WeatherSample {
  float tempC;
  float humidityPct;
  float windKph;
};

struct AirSample {
  float pm25;
  float pm10;
  int16_t aqi;
};

// A single value from a single source, at a single moment.
//
// The payload is a union rather than one struct per source because a FreeRTOS
// queue has a fixed element size chosen when it is created. Discovering later
// that the largest payload grew means resizing the queue and re-testing its
// overflow behaviour, so the worst case is worth fixing now while there is
// nothing to break.
//
// timestampMs is not decoration. Downstream, a reading's age decides how much
// the verdict is allowed to trust it - a source that quietly stopped updating
// must not keep feeding confident answers.
struct Reading {
  SourceId source;
  uint32_t timestampMs; // millis() when the source produced this
  bool valid;           // false = the source is reporting its own failure
  union {
    WeatherSample weather;
    AirSample air;
  };
};
