# sinks/serial

The consumer. Drains the pipeline and prints what arrives.

```cpp
#include "sinks/serial/serial_sink.h"
bool serialSinkStart();
```

Milestone 1's sink, and deliberately dumb. It exists to prove the pipeline end to end before a fusion stage or a display is worth writing.

## Output

A line per reading, with the age at the moment it was printed:

```
[  10231] weather  21.3 C  64% RH  11.2 km/h   (age 4 ms)
[  10500] weather  FAILED
```

And a heartbeat every 5 seconds when nothing arrives:

```
[  12034] idle     queued=0 dropped=0 stack_free=2196
```

The heartbeat is not decoration. Silence is the one output that carries no information — "nothing is happening" and "the firmware died" look identical — so the idle path prints queue depth, cumulative drops and remaining stack instead.

`stack_free` is in words, not bytes. A value trending toward zero is the only warning you get before a stack overflow, which otherwise presents as an unexplained reset with nothing on the wire.

## Priority

Runs below the source tasks. Producing readings is more time-sensitive than printing them, and a consumer that outranked its producers would be the wrong way round.
