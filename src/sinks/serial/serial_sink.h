#pragma once

// Starts the consumer task: drains the pipeline and prints what arrives.
//
// Milestone 1's sink. It exists to prove the shape end to end - source task,
// queue, consumer - before a fusion stage or a display is worth writing.
bool serialSinkStart();
