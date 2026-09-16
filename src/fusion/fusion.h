#pragma once

#include "core/snapshot.h"

// The queue's one consumer. Drains readings as they arrive and folds each
// into a snapshot; everything downstream reads the snapshot, never the queue.
//
// That is deliberate. A second queue consumer would steal readings from the
// first, so instead the fusion task owns the drain and hands out copies of
// its state. A sink, the verdict engine and the log all attach the same way.
bool fusionStart();

// Copies the current snapshot out under the lock. The copy is small and the
// caller can then read it at leisure without holding anything.
void fusionSnapshot(Snapshot &out);
