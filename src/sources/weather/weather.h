#pragma once

// Starts the weather source task: polls Open-Meteo on a fixed cadence and
// posts a Reading for every attempt, successful or not.
//
// Failures are posted too, with valid = false. A source that goes quiet when
// it breaks is indistinguishable downstream from a source that is merely slow,
// and the difference matters once a verdict depends on it.
bool weatherSourceStart();
