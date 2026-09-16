# fusion

The queue's one consumer. Drains readings as they arrive and folds each into a snapshot that everything downstream reads.

```cpp
#include "fusion/fusion.h"

bool fusionStart();
void fusionSnapshot(Snapshot &out);   // copy under the lock
```

## Why one consumer

A FreeRTOS queue delivers each element to exactly one receiver. Two tasks draining the same queue would each see half the readings, so fan-out has to happen after the queue. The fusion task owns `pipelineTake()`; sinks — and later the verdict engine and the log — call `fusionSnapshot()` and get a copy.

## The lock

Held for `snapshotApply()` on the write side and for one struct assignment on the read side. Neither does I/O, neither reads a clock, so the hold time is a few dozen instructions. A reader that wanted a pointer to the live snapshot instead of a copy would hold the lock for as long as it takes to print, which would block fusion behind the serial port.

## Output

One line per arrival — the observable for TC-1.2, "a reading posted by a source reached the consumer":

```
[  10231] weather  21.3 C  64% RH  11.2 km/h
[  40511] air      FAILED
```

And a heartbeat when nothing arrives for 5 seconds:

```
[  12034] idle     queued=0 dropped=0 stack_free=2196
```

Silence is the one output that carries no information. The heartbeat exists so that "nothing is happening" and "the firmware died" do not look identical.

## Priority

Below the sources, above the sink. Producing a reading has a cadence to keep; folding it can wait a tick; printing it can wait longer.
