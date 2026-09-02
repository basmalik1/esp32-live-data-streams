#pragma once

#include <stddef.h>

#include "core/reading.h"

// Parses an Open-Meteo /v1/forecast response into a WeatherSample.
//
// Deliberately free of Arduino and networking headers so it compiles and runs
// on the host. Response parsing is where malformed input, missing fields and
// unexpected types actually bite, and none of those need a board to reproduce -
// they need a fixture and a test.
//
// Returns false if the payload is not valid JSON or does not contain the
// fields this source depends on. A partial parse is treated as a failure
// rather than silently yielding zeros, because a zero reading downstream is
// indistinguishable from a real measurement of zero.
bool weatherParse(const char *json, size_t len, WeatherSample &out);
