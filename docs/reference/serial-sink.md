# sinks/serial

Prints the verdict and the fused snapshot every 10 seconds.

```cpp
#include "sinks/serial/serial_sink.h"
bool serialSinkStart();
```

It reads the snapshot, not the queue. The queue has one consumer — the [fusion task](fusion.md) — and every output surface takes a copy of what fusion knows. This is the same shape the verdict engine and the log will use, so the sink is the first proof that the fan-out works.

## Output

A header with the pipeline's health, the verdict, then one line per source:

```
[  50000] snapshot queued=0 dropped=0 stack_free=2840
  verdict  GOOD      confidence low   air missing
  weather  ok        21.3 C  64% RH  11.2 km/h  age 39s
  air      never     (2 failed)
```

The verdict line is `verdictFrom(snapshot, now)` computed on the spot — see [verdict](verdict.md). Reasons list what could not be trusted first, then what the conditions are.

Status is one of `never`, `ok`, `FAILING`, `STALE` — see [snapshot](snapshot.md) for what each means. A source that has never succeeded shows its failure count instead of a value, because the value fields mean nothing yet.

`dropped` climbing means fusion cannot keep up. `stack_free` is in words, not bytes; a value trending toward zero is the only warning you get before a stack overflow, which otherwise presents as an unexplained reset with nothing on the wire.

## Priority

Lowest of the application tasks. Printing is the one job here that can always wait.
