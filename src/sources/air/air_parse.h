#pragma once

#include <stddef.h>

#include "core/reading.h"

// Parses an Open-Meteo /v1/air-quality response into an AirSample.
//
// Same rules as the weather parser, for the same reasons: no Arduino headers
// so it runs on the host, and every field checked before any is used, because
// a missing key would otherwise come back as 0 - and 0 µg/m³ is a real,
// excellent reading rather than an error.
bool airParse(const char *json, size_t len, AirSample &out);
