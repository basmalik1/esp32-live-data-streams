#pragma once

// Prints the fused snapshot to serial on a fixed cadence.
//
// It reads the snapshot, not the queue. The queue has exactly one consumer -
// the fusion task - and every output surface takes a copy of what fusion
// knows. That is the same shape the verdict engine and the log will use.
bool serialSinkStart();
