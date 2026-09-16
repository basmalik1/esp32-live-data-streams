#pragma once

// Starts the air-quality source task: polls Open-Meteo's air-quality endpoint
// on its own cadence and posts a Reading for every attempt, successful or not.
//
// It is the second source on purpose. One source proves the pipeline; two on
// different schedules, failing independently, is what the fusion stage has to
// cope with.
bool airSourceStart();
