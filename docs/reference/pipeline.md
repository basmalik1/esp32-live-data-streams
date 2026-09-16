# core/pipeline

The single queue every source posts to and the fusion task drains. The only thing the two ends share.

```cpp
#include "core/pipeline.h"

bool     pipelineInit(uint32_t depth);
bool     pipelinePost(const Reading &r);              // from source tasks
bool     pipelineTake(Reading &out, uint32_t timeoutMs);  // from fusion, and only fusion
uint32_t pipelineDropped();
uint32_t pipelineQueued();
```

## Overflow

`pipelinePost()` never blocks. When the queue is full it discards the oldest entry to make room and increments the drop counter.

That policy is a decision, not a default. Blocking the producer would stall a source whose timing is not under our control. Dropping the newest would freeze the picture at the moment the consumer stalled — data that looks present and is wrong. Dropping the oldest costs history and preserves current state, which is the right trade for telemetry.

## Locking

The common path — room available — takes no lock, so producers do not serialise against each other.

The mutex is taken only when the queue is full. Making room is a receive followed by a send, and two producers arriving together without the lock would each discard an entry: two readings lost to admit two. The lock is held across two non-blocking queue calls and nothing else. If it cannot be acquired within 10 ms the post is abandoned rather than allowed to block a producer.

## Depth

Set at `pipelineInit()`. At v2.0 the depth is 16 against two sources polling every 10 and 15 minutes, which is well over half an hour of history — deliberately generous, because the interesting behaviour only appears when a push source starts producing faster than anything drains it.

## Instrumentation

`pipelineDropped()` and `pipelineQueued()` exist to be printed. A drop count that climbs is a fact about the consumer being too slow to keep up, and the only thing worse than knowing that is not knowing it.
