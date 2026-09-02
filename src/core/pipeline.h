#pragma once

#include <stdint.h>

#include "core/reading.h"

// The single queue every source posts to and the fusion stage drains.
//
// Overflow policy is DROP OLDEST. For telemetry the freshest reading is the
// useful one, so a slow consumer should cost you history rather than current
// state - and a producer must never block, because a source may be driven by
// hardware that cannot afford to wait on a display.
//
// The alternative policies are worth knowing you rejected: blocking the
// producer stalls a real-time source, and dropping the newest means a stalled
// consumer freezes the picture at the moment it stalled, which is the worst of
// both.
bool pipelineInit(uint32_t depth);

// Called from source tasks. Never blocks. Returns false only if the queue was
// never created.
bool pipelinePost(const Reading &r);

// Called from the consumer. Blocks up to timeoutMs waiting for a reading.
bool pipelineTake(Reading &out, uint32_t timeoutMs);

// Readings discarded to make room. A non-zero and climbing value means the
// consumer cannot keep up, which is a design fact worth surfacing rather than
// hiding.
uint32_t pipelineDropped();

// How many readings are queued right now, for the status surface.
uint32_t pipelineQueued();
