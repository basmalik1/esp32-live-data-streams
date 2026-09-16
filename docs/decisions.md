# Decision log

Every design choice made so far, with the reasoning and what was rejected. The code says *what*; this says *why*, which is the part that cannot be recovered from a diff.

Each entry ends with a **revisit** note — the condition under which the decision should be reconsidered. A decision without one is a decision nobody will ever re-examine.

Status as of v2.0: two sources on different cadences, one queue, a fusion task holding a snapshot, a sink that reads it. Thirty-one host tests passing. Nothing yet run on hardware.

---

## 1. What the project is

### 1.1 A verdict, not a dashboard

**Decision.** The device answers a question — *is it good out right now?* — with a single judgement and the reasons behind it. It is not a display of readings.

**Why.** A dashboard never has to decide what to do when one input is stale or missing; it just shows a number that stopped changing. A verdict forces every hard question — how old is too old, what happens when a source fails, how much confidence survives a missing input — to be answered explicitly. Those are the questions worth practising, and they are invisible in a dashboard design.

**Rejected.** A multi-source dashboard. Easier, more immediately impressive, and it would have exercised none of the concurrency problems this project exists to exercise.

**Revisit.** Never, for this project. It defines the project.

### 1.2 Online services as sources

**Decision.** Every input arrives over WiFi from a public API. No sensors, no additional hardware.

**Why.** Remote services have exactly the properties that make concurrent design necessary — independent timing, independent failure, no control over either — and they can be obtained today, replayed deterministically, and tested against captured fixtures on a laptop. A sensor has to be bought, wired and debugged before it teaches anything about scheduling.

**Rejected.** Starting with physical sensors. That confuses two problems: getting a driver to work, and designing a pipeline that survives a driver misbehaving. Doing them together means never being sure which one is broken.

**Revisit.** When adding a first physical source. The pipeline should not need to change; if it does, this decision was implemented wrongly.

### 1.3 Open-Meteo as the first source

**Decision.** Weather from `api.open-meteo.com`.

**Why.** No API key for non-commercial use, a separate air-quality endpoint for the second source, and a stable JSON shape. The single biggest friction at the start of a project like this is a service that wants a card number before it returns anything.

**Rejected.** Weather services requiring keys, and anything with a rate limit tight enough to matter at one request per ten minutes.

**Revisit.** If a source is needed that Open-Meteo does not provide, or if the free tier's terms change.

### 1.4 Iterations in this order

**Decision.** v1.0 pipeline → v2.0 fusion → v3.0 verdict → v4.0 streaming and persistence.

**Why.** Each iteration needs the previous one to be testable. Fusion is meaningless with one source. A verdict needs a snapshot to judge. Streaming is a stress test of a pipeline that must already be correct. Persistence last, because a log format should be designed around what actually needs recording, which is not known until the verdict exists.

**Rejected.** Building the push stream early to get the "hard" concurrency problem out of the way. It would have meant designing overflow behaviour against a consumer that did not yet exist.

**Revisit.** After v2.0. If fusion turns out to need the log for debugging, persistence moves up.

### 1.5 Dashboard deferred

**Decision.** No web UI until the pipeline, fusion and verdict work end to end.

**Why.** The output surface is the least important design question and the most time-consuming to build. Deciding it early would have shaped the data model around presentation rather than around what the verdict needs.

**Rejected.** Reusing an existing dashboard. Explicitly deferred at project start, on the grounds that the right presentation is not knowable before the verdict exists.

**Revisit.** At v3.0, once there is a verdict to display.

---

## 2. Architecture

### 2.1 Tasks, not a superloop

**Decision.** One FreeRTOS task per source. No shared polling loop.

**Why.** A loop that polls services in turn lets the slowest one set the pace for all of them, and one hung request stops everything. Independent tasks remove both: each source keeps its own schedule, and a task stuck on a socket blocks nothing but itself. This is the founding decision; every other entry in this section follows from it.

**Rejected.** A superloop with non-blocking HTTP and a state machine per source. Workable, but it reimplements a scheduler badly and the state machines grow to swallow the logic.

**Revisit.** Never, for this project. It is the point.

### 2.2 A single queue is the only coupling

**Decision.** Sources call `pipelinePost()`. The consumer calls `pipelineTake()`. Neither knows the other exists.

**Why.** Adding a source means writing a task that posts the same struct; adding a consumer means draining the same queue. Nothing else changes. The queue is also the one place backpressure can be observed and controlled.

**Rejected.** A queue per source, drained by a consumer that polls each. That reintroduces the ordering and starvation problems of a superloop one level up. Also rejected: sources writing directly into a shared snapshot under a mutex — which works, but loses the history between reads and makes overflow behaviour impossible to define.

**Revisit.** If a second consumer needs readings the first has already taken. The answer is probably a fan-out at the fusion stage, not a second queue.

### 2.3 Overflow policy: drop oldest

**Decision.** When the queue is full, discard the oldest entry to admit the new one.

**Why.** For telemetry the freshest reading is the useful one. A slow consumer should cost history, not current state.

**Rejected.** *Blocking the producer* — stalls a source whose timing may not be under our control, and a blocked source task holds its schedule slot. *Dropping the newest* — freezes the picture at the moment the consumer stalled, so the data looks present and is wrong, which is the worst possible failure. *Unbounded queue* — turns a slow consumer into an out-of-memory reset with no warning.

**Revisit.** At v4.0, when a push stream produces faster than anything drains it. Drop-oldest is right for state; it may be wrong for a stream where every event matters, and that stream may need its own queue with its own policy.

### 2.4 Lock-free fast path, mutex only on overflow

**Decision.** `pipelinePost()` takes no lock when the queue has room. A mutex is taken only on the full path.

**Why.** The common case should not serialise producers against each other — that would reintroduce coupling through the back door. But making room is two operations, a receive then a send, and two producers arriving together without a lock would each discard an entry: two readings lost to admit two. The lock is held across two non-blocking queue calls and nothing else, with a 10 ms timeout after which the post is abandoned rather than allowed to block.

**Rejected.** Locking every post — simpler, and wrong for the reason above. No lock at all — the double-discard is a real race, not a theoretical one, once there are several sources.

**Revisit.** If the drop counter climbs under load and the lock timeout is the cause. That would mean producers are contending on the slow path constantly, which is a queue-depth problem, not a locking problem.

### 2.5 Drops are counted and printed

**Decision.** `pipelineDropped()` exists and the consumer prints it in its heartbeat.

**Why.** A climbing drop count is a design fact — the consumer is too slow — and the only thing worse than knowing it is not knowing it. Hiding the counter would turn a measurable problem into a mysterious one.

**Rejected.** Logging drops to serial as they happen. At high rates that floods the console and slows the consumer further, making the problem it reports worse.

**Revisit.** Never. Instrumentation is cheap and its absence is expensive.

### 2.6 One consumer; everything else reads the snapshot

**Decision.** The fusion task is the only caller of `pipelineTake()`. Sinks, and later the verdict and the log, call `fusionSnapshot()` and receive a copy of its state.

**Why.** A FreeRTOS queue delivers each element to exactly one receiver, so a second task draining it would steal readings from the first. Fan-out has to happen after the queue, and the cheapest place is a struct copy under a mutex: about a hundred bytes, held for one assignment. A copy also means the verdict can be a pure function of a `Snapshot` and a time, which is what makes it host-testable.

**Rejected.** *A second queue per consumer, fed by fusion* — more memory, more policy decisions, and every consumer still ends up rebuilding the same snapshot from the stream. *Handing out a pointer to the live snapshot* — cheaper than a copy, and a reader would then hold the lock for however long it takes to print, blocking fusion behind the serial port.

**Revisit.** If a consumer needs the sequence of readings rather than the current state — the log sink at v4.0 may. Then it gets a tap on the fusion task, not a second queue consumer.

---

## 3. The reading

### 3.1 Every reading carries a timestamp

**Decision.** `Reading::timestampMs` is set by the source at the moment the value was obtained.

**Why.** Age is the only thing a consumer can use to decide whether a value still means anything. A reading without a timestamp cannot be judged stale, and a verdict built on it cannot know how much to trust it.

**Rejected.** Timestamping at the consumer on receipt. That measures queue latency, not data age, and the two diverge exactly when it matters — when the queue is backed up.

**Revisit.** If a source starts delivering values with their own timestamps (a server-side observation time). Then the reading may need both.

### 3.2 Timestamp after the fetch, not before

**Decision.** The source stamps the reading after the HTTP request returns.

**Why.** Stamping before the fetch would make a slow request look fresher than it is by however long the request took. Age must measure from when the value was known.

**Rejected.** Stamping at task wake-up, which is simpler and wrong by a variable amount.

**Revisit.** Never.

### 3.3 Failures are posted, not swallowed

**Decision.** A failed fetch posts a `Reading` with `valid = false`. Every poll produces exactly one reading.

**Why.** A source that goes silent when it breaks is indistinguishable downstream from one that is merely slow, and a consumer trying to decide whether its data still means something has no way to tell them apart. Posting the failure makes the difference explicit at the point where it is still cheap to act on. This is what REQ-3 is for, and it is the decision most likely to save the verdict from confidently stating something built on a dead input.

**Rejected.** Retrying silently until success. Hides the failure for as long as the retries take, which is exactly the window in which the last good value is going stale.

**Revisit.** Never. Reconsider the retry cadence, not the principle.

### 3.4 The payload is a union

**Decision.** `Reading` holds a union of per-source sample structs.

**Why.** A FreeRTOS queue copies fixed-size elements. Fixing the element size now, while there is nothing to break, means the queue's overflow behaviour is tested against the real element size from the start. Discovering later that the largest payload grew means resizing the queue and re-running every load test.

**Rejected.** A separate queue type per source — reintroduces the fan-in problem. A `std::variant` — fine on the host, but pulls in more C++ runtime than the target build wants. A pointer to a heap-allocated payload — puts allocation on the producer path, which is precisely where allocation should never be.

**Revisit.** If a source needs a payload much larger than the others, so that every queue slot is sized for the worst case. Then that source may warrant its own path.

### 3.5 The `valid` flag rather than a sentinel value

**Decision.** Validity is an explicit boolean, not a magic value like `NaN` or `-999`.

**Why.** Sentinels are conventions that every reader has to know and every writer has to remember. A boolean is checked by the compiler and read by a human.

**Rejected.** `NaN` — works for floats, means nothing for the integer fields, and silently propagates through arithmetic.

**Revisit.** Never.

### 3.6 The snapshot keeps last-good and last-attempt separately

**Decision.** Per source, the snapshot holds the most recent *valid* reading and, separately, when the last attempt was and whether it succeeded. A failure never overwrites the last good value.

**Why.** A verdict wants two different things from a source: what it last said, and whether it can still be believed. Collapsing those into one field loses one of them. Keeping them apart gives four distinguishable states — never seen, ok, failing with a usable last value, stale — and the verdict needs all four: "the service is down but we knew the temperature five minutes ago" is a different situation from "we have not heard anything for an hour".

**Rejected.** *Overwriting on failure* — turns a single dropped request into a blank, which is precisely the information loss REQ-3 forbids. *Keeping a history* — more than the verdict needs, and the log is the right place for history when it arrives.

**Revisit.** If a source's failures need to be reasoned about in more detail than a count — a source that fails in a particular pattern, say. Then the attempt record grows.

### 3.7 Stale means three missed polls, per source

**Decision.** A source's last good value is stale once it is older than three poll periods: thirty minutes for weather, forty-five for air. A stale source outranks a failing one.

**Why.** One missed poll is a transient on a home network and should not change anything. Three in a row means the source is gone and its last value has stopped describing the present. Tying the threshold to the poll period, rather than to a guess about how fast the world changes, gives it a rationale that survives a cadence change. The thresholds live in `snapshot.cpp` beside the rule that reads them, so the numbers and the reasoning are in one place.

Stale outranks failing because a source that is both old and erroring is old. The verdict should treat it as absent, not as a recent value with a hiccup.

**Rejected.** *A single global threshold* — a forecast and an hourly air-quality index do not age at the same rate, and one number would be wrong for at least one of them. *Thresholds derived from how fast the quantity changes* — the initial intuition was that air quality goes stale faster than weather. The service itself only updates hourly, so that intuition was backwards, and a rule based on the poll period does not depend on getting it right.

**Revisit.** After the first extended run. If a source misses three polls routinely on a healthy network, the retry cadence is wrong, not the threshold.

---

## 4. Scheduling

### 4.1 `vTaskDelayUntil`, not `vTaskDelay`

**Decision.** Source tasks hold a fixed period using `vTaskDelayUntil`.

**Why.** `vTaskDelay(period)` after a fetch yields a period of *fetch + delay*, which drifts with network conditions. `vTaskDelayUntil` measures from the previous wake, so a slow fetch shortens the wait rather than lengthening the period.

**Rejected.** A software timer posting to the queue on a fixed tick, with the fetch happening in the timer callback — timer callbacks must not block, and an HTTP request blocks.

**Revisit.** If a source's fetch ever takes longer than its period. Then the cadence is unachievable and the period, not the mechanism, is wrong.

### 4.2 Application tasks pinned to core 1

**Decision.** Every task this project creates runs on core 1.

**Why.** On the ESP32 the WiFi driver and lwIP run on core 0. Keeping application tasks off it means a busy source cannot starve the network stack that all sources depend on.

**Rejected.** Letting the scheduler place tasks freely. Fine until it is not, and when it is not the failure is a timing-dependent one that does not reproduce on demand.

**Revisit.** If core 1 saturates. Then the question is which tasks are safe to co-locate with the network stack, not whether to pin at all.

### 4.3 Sources outrank the consumer

**Decision.** Source tasks run at a higher priority than the sink.

**Why.** Producing a reading is time-sensitive — it has a cadence to keep. Printing one is not. A consumer that outranked its producers would be the wrong way round: it would win the CPU to print a reading while the source that produces the next one waits.

**Rejected.** Equal priority with round-robin. Loses the guarantee that a source always gets to post.

**Revisit.** When a consumer becomes time-sensitive — a display with a refresh deadline, or the log sink if writes must not be delayed. Then it may need to outrank the *slower* sources but not the fast ones, which means priority tiers rather than two levels.

### 4.4 Explicit stack sizes, measured not assumed

**Decision.** Every task declares its stack size, and the consumer prints its own high-water mark on every heartbeat.

**Why.** A stack overflow on an ESP32 presents as an unexplained reset with nothing on the wire. The high-water mark is the only warning available, and it is free.

**Rejected.** Generous stacks everywhere. RAM is 320 KB, TLS alone wants a large chunk, and "generous" for a task that does HTTPS is a number nobody can guess.

**Revisit.** After the first extended run on hardware. Every task now prints its own mark — the sources after each fetch, when it is lowest — so the 8 KB guesses become measurements the moment the firmware runs.

### 4.5 `loop()` suspends itself

**Decision.** The Arduino `loop()` calls `vTaskDelay(portMAX_DELAY)` and never returns.

**Why.** Every unit of work lives in a task with its own stack, priority and schedule. The loop task has nothing to do, and letting it spin would cost a slice every tick for no reason.

**Rejected.** Putting the consumer in `loop()`. Works, but then one consumer is special — it lives in a task with a stack size and priority the project did not choose.

**Revisit.** Never.

---

## 5. Sources

### 5.1 The parser is a free function with no Arduino headers

**Decision.** `weatherParse()` lives in its own translation unit that includes only ArduinoJson and the reading struct.

**Why.** It can be compiled and run on a development machine, which is the only practical way to test it against the responses that matter: a body truncated by a dropped connection, a field the service omitted, a number that arrived as a string. Reproducing those against a live API means waiting for a bad day.

**Rejected.** Parsing inline in the source task. Testable only on the board, with a real network, against whatever the service happens to return that minute.

**Revisit.** Never. This is the single decision that makes the host test tier possible.

### 5.2 Reject a response with a missing field

**Decision.** If any required field is absent, the parser returns `false` and leaves the caller's struct untouched.

**Why.** Open-Meteo omits keys it has no data for rather than sending nulls. A permissive parser would return `0.0` for the missing field — and `0.0 °C` is indistinguishable downstream from a real freezing measurement. There is no safe default for a value that means something.

**Rejected.** Partial results with per-field validity flags. More flexible, and it moves the "is this field trustworthy" question to every consumer instead of answering it once.

**Revisit.** If a source has genuinely optional fields. Then a partial-result type is warranted for that source, not a relaxation of this rule.

### 5.3 Ten-minute cadence, thirty-second retry

**Decision.** Poll every ten minutes on success; retry after thirty seconds following a failure.

**Why.** A forecast does not change faster than that, and the service should not be hammered. But a source that has just failed should recover quickly — a ten-minute wait after a transient error leaves the pipeline degraded for no reason.

**Rejected.** Exponential backoff. Correct for a service that is overloaded; wrong for one that dropped a single request, which is far more common on a home network.

**Revisit.** If the retry ever triggers a rate limit. Then backoff is warranted.

### 5.4 TLS without certificate validation

**Decision.** The HTTPS client calls `setInsecure()`.

**Why.** Pinning a root CA means shipping a certificate that expires, and an expiry is a field failure with no warning. For public, read-only weather data, an attacker who can intercept the connection gains the ability to lie about the weather.

**Rejected.** Pinning. The maintenance cost is real and the threat it defends against is not one this project faces.

**Revisit.** If a source ever carries data that matters, or if the device ever acts on a reading in a way that could cause harm. Recorded as a known gap in the process document so it cannot be forgotten.

### 5.5 Coordinates live in the gitignored secrets file

**Decision.** `LOCATION_LATITUDE` and `LOCATION_LONGITUDE` are in `include/secrets.h`, alongside the WiFi credentials.

**Why.** Coordinates are location data. A public repository is a poor place to publish where you live, and the forecast grid is coarse enough that precision buys nothing anyway.

**Rejected.** A committed config header. Convenient and a privacy mistake.

**Revisit.** Never.

### 5.6 Air quality every fifteen minutes

**Decision.** The second source polls Open-Meteo's air-quality endpoint on a fifteen-minute period, against weather's ten.

**Why.** The service updates hourly, so anything faster than that is wasted requests; fifteen minutes catches each update within a quarter of an hour without hammering. The more important reason is that it is *different* from ten. Two sources on the same period arrive in lockstep and never produce the case fusion exists for — one input fresh, the other not.

**Rejected.** Matching weather's ten minutes. Simpler to reason about and it hides the interesting behaviour.

**Revisit.** Never for the principle. The number can move if the service's update interval changes.

### 5.7 One HTTPS helper for every source

**Decision.** `httpsGet()` in `net/` does the TLS client setup, the request and the status check. Sources build a URL and parse a body; nothing else.

**Why.** The second source would have copied thirty lines of client setup from the first, and with it the `setInsecure()` call. A known gap should exist in exactly one place, so that fixing it is one edit and forgetting one copy is impossible.

**Rejected.** Per-source clients. Two copies today, three at v4.0, and each is a place the TLS decision could silently diverge.

**Revisit.** If a source needs a different transport — a WebSocket for the push stream, say. That is a second helper, not a reason to inline this one.

---

## 6. Observability

### 6.1 A heartbeat when idle

**Decision.** When nothing arrives within five seconds, the consumer prints queue depth, cumulative drops and its stack headroom.

**Why.** Silence is the one output that carries no information. "Nothing is happening" and "the firmware died" look identical on a serial monitor, and the heartbeat costs one line every five seconds to tell them apart. It also makes the three most useful numbers visible without asking.

**Rejected.** Silence when idle, on the grounds that it is cleaner. It is cleaner right up until something stops working.

**Revisit.** When there is a status surface other than serial. Then the heartbeat can move there.

---

## 7. Process

### 7.1 V-Model, with the unflattering numbers written down

**Decision.** Requirements, a traceability matrix and a fifteen-case test plan exist from v1.0, and the plan states that one case is automated.

**Why.** A matrix that lists only what already passes is not tracing anything. Writing "1 of 15" down makes it awkward for that to stay true quietly, and lists the twelve cases belonging to iterations not yet built so their absence is visible.

**Rejected.** Writing the test plan as each iteration lands. Produces a plan that always matches the code and therefore never catches anything.

**Revisit.** Never. Update the numbers; keep the principle.

### 7.2 Two test tiers by what each can prove

**Decision.** Host tests for pure logic, on-target tests for the scheduler and peripherals. The host environment compiles only files matching `sources/*/*_parse.cpp`, so anything Arduino-dependent is excluded by construction.

**Why.** Host tests run in seconds and can be fed inputs a live system will not produce on demand. On-target tests are slow — a full build, flash and run per file — but they are the only honest answer to questions about queue behaviour under a real flood or stack headroom on real silicon.

**Rejected.** Mocking the Arduino API on the host to test everything there. The mock would need to model the scheduler to test scheduling, at which point it is a second implementation with its own bugs.

**Revisit.** When a system tier becomes meaningful — at v3.0, once there is a verdict to assert against.

### 7.3 Documentation stands alone

**Decision.** Nothing in the repository refers to other work, even where a decision was informed by it. Reasons are stated on their own terms.

**Why.** A reader of this repository does not have that context. A comment that says "for the same reason as before" explains nothing and signals that the author did not think the reason through here.

**Rejected.** Cross-referencing. Cheaper to write, and it makes every decision look borrowed rather than made.

**Revisit.** Never.

---

## Open questions

Decisions not yet made, listed so they are made deliberately rather than by default.

- **Queue depth at v4.0.** Sixteen is generous for polled sources and will be nothing against a push stream. Whether the stream shares the queue or gets its own is undecided.
- **Stack sizes.** All estimates. Every task now reports its own high-water mark; the first extended hardware run should replace the estimates with measurements.
- **What the verdict says.** The snapshot can now tell the verdict what it knows and how much to trust it. What "good out" means — which thresholds on temperature, wind and AQI, and how a failing or stale input degrades the answer — is v3.0's first decision.
