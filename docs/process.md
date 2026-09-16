# Process

Built as a V-Model exercise: each iteration runs requirements → design → implementation → test, and every requirement is traced to the code that satisfies it and the test case that proves it. Where a test has not been run, this document says so. Where it has, the evidence is quoted.

Four iterations, each adding one capability the previous one cannot support:

| Iteration | Capability added |
| --- | --- |
| v1.0 | Pipeline — one source, one queue, one consumer |
| v2.0 | Fusion — several sources on independent schedules, combined into a snapshot |
| v3.0 | Verdict — a single judgement derived from the snapshot |
| v4.0 | Streaming and persistence — a push source, backpressure, and a replayable log |

## The problem

A device that gathers from several remote services has a harder job than a device that reports on itself. The services answer at different speeds, some fail while others succeed, and none of them are under your control. A single loop polling each in turn is wrong in a specific way: the slowest service sets the pace for everything, and one hung request stops the world.

That is what an RTOS is for, and it is why the architecture here is a set of independent tasks feeding one queue rather than a sequence of calls.

## Requirements

| ID | Requirement | Status |
| --- | --- | --- |
| REQ-1 | The system shall acquire readings from multiple independent sources, each on its own schedule, with no source able to delay another | Met — two sources on different cadences |
| REQ-2 | Every reading shall carry the time it was obtained, and consumers shall be able to determine its age | Met |
| REQ-3 | A source failure shall be reported as a failure, distinguishable from a source that has simply not updated yet | Met |
| REQ-4 | No source shall block on a slow or stalled consumer | Met |
| REQ-5 | The system shall produce a single verdict from available readings, and shall degrade that verdict when inputs are stale or absent | Met |
| REQ-6 | Readings and verdicts shall be logged to non-volatile storage and be replayable offline | Not started — v4.0 |

REQ-3 and REQ-5 are the two that shape everything else. A system that cannot tell a broken sensor from a quiet one will eventually state a confident answer built on a value that stopped updating hours ago, and that failure is silent by construction — which is the worst kind.

## Traceability

| Requirement | Module | Test cases |
| --- | --- | --- |
| REQ-1 | `sources/weather`, `sources/air`, `core/pipeline`, `fusion` | TC-1.1, TC-1.2, TC-1.3 |
| REQ-2 | `core/reading`, `core/snapshot` | TC-2.1, TC-2.2, TC-2.3 |
| REQ-3 | `sources/*`, `core/snapshot` | TC-3.1, TC-3.2, TC-3.3 |
| REQ-4 | `core/pipeline` | TC-4.1, TC-4.2 |
| REQ-5 | `core/verdict`, `sinks/serial` | TC-5.1, TC-5.2, TC-5.3 |
| REQ-6 | `core/log` *(not yet written)* | TC-6.1, TC-6.2, TC-6.3 |

## Test plan

| ID | Level | What it checks | Status |
| --- | --- | --- | --- |
| TC-1.1 | Unit | Each response parser accepts a real payload and rejects malformed, truncated, mistyped and incomplete ones | **Automated** — host |
| TC-1.2 | Integration | A reading posted by a source task is received by the consumer | **Passed** — live run |
| TC-1.3 | System | A source holds its cadence across an extended run without drift | **Passed** — 30-minute soak, offset under 0.4 s |
| TC-2.1 | Unit | Age arithmetic stays correct across the `millis()` rollover | **Automated** — host |
| TC-2.2 | Integration | The age reported by the consumer matches the elapsed time since acquisition | **Passed** — live run |
| TC-2.3 | Unit | The snapshot ages each source against its own threshold, independently of the others | **Automated** — host |
| TC-3.1 | Unit | A failed fetch produces a reading with `valid = false` rather than no reading | **Automated** — on-target |
| TC-3.2 | Integration | A forced HTTP error surfaces downstream as a failure, not as silence | **Automated** — on-target |
| TC-3.3 | Unit | A failure keeps the last good value and is reported as failing — distinguishable from a source never heard from and from one that is merely stale | **Automated** — host |
| TC-4.1 | Unit | A full queue discards the oldest entry and increments the drop counter | **Automated** — on-target |
| TC-4.2 | Integration | A producer posting faster than the consumer drains never blocks | **Automated** — on-target |
| TC-5.1 | Unit | The verdict engine, across a table of snapshots covering every staleness and missing-input combination | **Automated** — host |
| TC-5.2 | Integration | A stale input degrades the live verdict | **Automated** — on-target |
| TC-5.3 | System | Network removed mid-run: the verdict hedges rather than remaining confident | **Automated** — on-target |
| TC-6.1 | Unit | A log record survives a write/read round trip | Not started |
| TC-6.2 | Integration | The log survives power loss mid-write | Not started |
| TC-6.3 | System | Replaying a recorded log reproduces the original verdicts exactly | Not started |

**Eleven of seventeen are automated** — five on the host, six on the board — **and three more passed as observed live runs.** One of fifteen at v1.0, four at v2.0, five at v3.0. The three that remain need the log. The on-target tier found one bug before it reached a commit: see TC-5.3.

## Results

### TC-1.1 result

Eight cases for the weather parser against captured Open-Meteo responses, run on the host in 2 s:

```
test_parses_a_real_response                            PASSED
test_accepts_integer_valued_fields                     PASSED
test_rejects_malformed_json                            PASSED
test_rejects_truncated_response                        PASSED
test_rejects_response_with_no_current_block            PASSED
test_rejects_missing_field_rather_than_defaulting_to_zero  PASSED
test_rejects_wrong_type                                PASSED
test_rejects_empty_input                               PASSED
```

Two of those are worth more than the rest. **Truncation** is what a dropped connection actually produces — valid-looking JSON that simply stops — and it is tedious to reproduce against a live service. **A missing field** is the dangerous one: the service omits a key it has no data for, and a permissive parser reports `0.0 °C`, which is indistinguishable downstream from a real freezing measurement. The parser rejects the response instead and leaves the caller's struct untouched.

The air-quality parser has the same nine cases (one more: a fractional AQI is rejected, because the index is an integer by definition and a fractional one means the response is not what we think it is). Missing-field matters even more here — `0 µg/m³` is a real and excellent reading, not an obvious error.

### TC-2.1, TC-2.3, TC-3.3 result

Fourteen cases in `test_native_snapshot`, run on the host in 2 s:

```
test_age_is_elapsed_time                                  PASSED
test_age_survives_millis_rollover                         PASSED
test_empty_snapshot_has_never_seen_anything               PASSED
test_good_reading_is_ok_and_keeps_its_value               PASSED
test_reading_goes_stale_past_its_threshold                PASSED
test_stale_reading_still_carries_its_last_value           PASSED
test_thresholds_differ_per_source                         PASSED
test_sources_age_independently                            PASSED
test_newer_reading_replaces_older                         PASSED
test_failure_after_good_reading_is_failing_not_blank      PASSED
test_failure_before_any_good_reading_is_still_never_seen  PASSED
test_stale_outranks_failing                               PASSED
test_recovery_clears_the_failure_count                    PASSED
test_failure_in_one_source_does_not_touch_the_other       PASSED
```

These exist because the snapshot takes time as a parameter instead of reading a clock. A source can be made forty-five minutes old, or pushed across the 49-day `millis()` wrap, in one line — neither is something to wait for on a board. The one that matters most is **failure-after-good**: the last known value survives a failed fetch and the source is reported as *failing*, which is a different thing from *stale* (old) and from *never seen* (nothing yet). The verdict needs all three told apart.

### TC-5.1 result

Fifteen cases in `test_native_verdict`, run on the host in 2 s:

```
test_mild_calm_clean_is_good_with_no_reasons          PASSED
test_temperature_bands_and_boundaries                 PASSED
test_cold_and_hot_name_themselves                     PASSED
test_wind_bands_and_boundaries                        PASSED
test_air_bands_and_boundaries                         PASSED
test_worst_factor_wins                                PASSED
test_humidity_does_not_move_the_verdict               PASSED
test_empty_snapshot_is_unknown_with_no_confidence     PASSED
test_failing_source_counts_at_low_confidence          PASSED
test_stale_source_is_excluded                         PASSED
test_missing_source_is_excluded                       PASSED
test_everything_stale_is_unknown                      PASSED
test_every_status_combination                         PASSED
test_reason_count_never_exceeds_capacity              PASSED
test_names_are_printable                              PASSED
```

`test_every_status_combination` is the one the test case is named for. It walks all sixteen combinations of source status — four for weather, four for air — with each source's value chosen to band Poor if used, so whether a source was admitted shows up in the outcome. It asserts the confidence rule, the admitted-or-excluded rule, and that every reason appears exactly when it should. `test_everything_stale_is_unknown` is TC-5.3's condition reproduced on the host: both sources aged past their thresholds, and the verdict says *Unknown* rather than repeating the last answer. What the host cannot check is that the live firmware behaves the same when the network is actually pulled — that is TC-5.3, below.

### TC-1.2, TC-2.2 result — first live run

The firmware's first boot on the ESP32-S3, captured over the UART port:

```
esp32-live-data-streams
wifi: connecting to BasitWifi 2.4...
wifi: connected, IP 10.0.0.101
wifi: rssi -25 dBm
air: stack_free=4100
[   3500] air      pm2.5 11.8  pm10 11.9  aqi 49
weather: stack_free=4016
[   3519] weather  22.2 C  88% RH  11.3 km/h
[   8519] idle     queued=0 dropped=0 stack_free=2052
[  10199] snapshot queued=0 dropped=0 stack_free=3324
  verdict  GOOD      confidence high
  weather  ok        22.2 C  88% RH  11.3 km/h  age 6s
  air      ok        pm2.5 11.8  pm10 11.9  aqi 49  age 6s
```

Both sources posted within 20 ms of each other, 3.5 s after boot, and the fusion task printed each arrival — TC-1.2. Seventy seconds later the sink printed `age 76s` for a reading stamped `[3519]` against a header stamped `[80199]`: 76.68 s elapsed, reported to the second — TC-2.2. `dropped=0` throughout.

### TC-3.1, TC-3.2 result — `test_embedded_sources`

```
test_failed_fetch_posts_an_invalid_reading_not_nothing  PASS
test_http_error_is_reported_as_failure                  PASS
```

The first case starts the weather task *before* joining the network and takes from the queue: a Weather reading with `valid = false` arrives inside the 5 s window, which is the point of REQ-3 — the failure was posted, not swallowed. The second joins, confirms a real endpoint returns a body, then asks for `/v1/does-not-exist`: `httpsGet` returns false and leaves the body empty.

### TC-4.1, TC-4.2 result — `test_embedded_pipeline`

```
test_full_queue_drops_oldest_and_counts_it              PASS
TC-4.2: posted=1000 taken=8 queued=0 dropped=992 max_post_us=18
test_producers_never_block_against_a_slow_consumer      PASS
```

TC-4.1: a queue of four, six posts, `dropped == 2`, and the four that remain are readings 3 through 6 in order.

TC-4.2: two producer tasks at priority 3 each post 500 readings as fast as they can into a queue of eight while a consumer at priority 1 drains with a 2 ms sleep. Every post is timed with the microsecond timer. The consumer got 8 — the producers outrank it and never yield, so it only runs once they are done, which is the harshest version of "slow consumer" the scheduler can produce. Every reading is accounted for (`1000 = 8 + 0 + 992`), and the slowest post took **18 µs**. The overflow lock's timeout is 10 ms, so a producer that had ever blocked would show up three orders of magnitude higher.

### TC-5.2, TC-5.3 result — `test_embedded_verdict`

```
[      0] weather  -10.0 C  0% RH  5.0 km/h
test_stale_input_degrades_the_live_verdict              PASS
[   6194] weather  21.8 C  88% RH  13.0 km/h
[   6363] air      pm2.5 11.8  pm10 11.9  aqi 49
TC-5.3: before  GOOD / high
[   6942] weather  FAILED
[   6942] air      FAILED
TC-5.3: after   GOOD / low
test_network_removed_mid_run_makes_the_verdict_hedge    PASS
```

TC-5.2 posts a −10 °C weather reading stamped at time 0 through the real queue, lets the real fusion task fold it, and asks for the verdict as of 31 minutes later. The reading is Stale, it is excluded, the verdict is Good at low confidence with `weather stale` — and *not* Poor, which is what the cold reading would have produced had it been admitted. Only the reading's origin is synthetic; the queue, task, mutex, copy and verdict are the live ones.

TC-5.3 is the system case, and everything in it is real: join the network, start both sources, wait for both to arrive, confirm `GOOD / high`. Then `WiFi.disconnect()`, tell both sources to poll now, and wait. Both post `FAILED`, and the verdict is `GOOD / low` with `weather failing, air failing` — the outcome stands because the last good values are seconds old, and the confidence does not. What comes after the thresholds is `test_everything_stale_is_unknown` on the host.

**This test found a bug.** The first version of the poll-now hook left the task's scheduled wake time in the future after an aborted delay, and FreeRTOS's `vTaskDelayUntil` treats a future wake time as "no delay needed" — so every forced poll fetched twice, 1 ms apart. The fix compares the clock to the wake time after every wait; the test now asserts exactly one failure per source. A host test could not have caught this: it is a property of the real scheduler.

### TC-1.3 result — 30-minute soak

The main firmware, left alone with the UART port captured for 36 minutes. One boot banner, `dropped=0` on every heartbeat, no failures. Arrival timestamps in ms:

| Source | Period | Arrivals | Intervals | Offset from ideal after the run |
| --- | --- | --- | --- | --- |
| weather | 600 s | 2957 · 601924 · 1201909 · 1802615 | 598.97 · 599.99 · 600.71 s | **−342 ms** after 3 periods |
| air | 900 s | 3034 · 901900 · 1802640 | 898.87 · 900.74 s | **−394 ms** after 2 periods |

The individual intervals wobble by about a second because the timestamp is taken *after* the fetch and a TLS handshake takes a variable time. What the case is about is whether that wobble accumulates, and it does not: after half an hour each source is within 0.4 s of where a perfect clock would put it. With `vTaskDelay` the period would have been 600 s *plus* the fetch, and the offset would already be several seconds and growing. This is the property `vTaskDelayUntil` was chosen for, and it holds on real silicon.

(A first attempt at this soak was cut short at 330 s by the capturing process, before any source had polled twice. It is mentioned because the process document should not read as though everything worked the first time.)

### Stack high-water marks — three runs

Minimum free stack in bytes, reported by each task at its own low point (sources after the TLS fetch, fusion and sink on every tick). Run 3 is the soak: four weather fetches, three air.

| Task | Allocated | Run 1 | Run 2 | Run 3 (soak) | Min free | Used at worst |
| --- | --- | --- | --- | --- | --- | --- |
| weather | 8192 | 4016 | 3952 | 3968 | 3952 | 4240 |
| air | 8192 | 4100 | 4048 | 3888 | 3888 | 4304 |
| fusion | 4096 | 2052 | 2052 | 2052 | 2052 | 2044 |
| serial sink | 4096 | 1964 | 1964 | 1964 | 1964 | 2132 |

Nine TLS handshakes across the two sources, and the worst of them used 4.3 KB of 8. Fusion and the sink are dead steady — their work is the same every tick. Every task keeps roughly half its stack, which is comfortable and still not enough evidence to shrink anything: air's minimum moved down by 160 bytes between runs 2 and 3, which is the kind of variation a longer run could extend. The sizes stay until several days of marks agree.

## Known gaps

**No certificate validation.** The HTTPS client calls `setInsecure()`, so the connection is encrypted but the server is not authenticated. Pinning a root CA means shipping a certificate that expires. For public read-only weather data the exposure is low, but it is a real weakness rather than an oversight.

**Stack sizes are measured, not yet sized.** Three runs, nine TLS handshakes — see the table above. Every task keeps roughly half its stack at the low point, which is comfortable but not proof that a longer run, a slower handshake or a larger response would not need more. The sizes stay where they are until several days of high-water marks say otherwise.
