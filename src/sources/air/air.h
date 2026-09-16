#pragma once

// Starts the air-quality source task: polls Open-Meteo's air-quality endpoint
// on its own cadence and posts a Reading for every attempt, successful or not.
//
// It is the second source on purpose. One source proves the pipeline; two on
// different schedules, failing independently, is what the fusion stage has to
// cope with.
bool airSourceStart();

// Cuts the current wait short so the next fetch happens now. The cadence
// restarts from the forced poll. This is what a "network just came back"
// handler calls, and what a test calls instead of waiting ten minutes.
void airSourcePollNow();
