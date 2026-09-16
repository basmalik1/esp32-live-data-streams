# Process

Built as a V-Model exercise: each iteration runs requirements → design → implementation → test, and every requirement is traced to the code that satisfies it and the test case that proves it. Where a test has not been run, this document says so.

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
| REQ-5 | The system shall produce a single verdict from available readings, and shall degrade that verdict when inputs are stale or absent | Not started — v3.0 |
| REQ-6 | Readings and verdicts shall be logged to non-volatile storage and be replayable offline | Not started — v4.0 |

REQ-3 and REQ-5 are the two that shape everything else. A system that cannot tell a broken sensor from a quiet one will eventually state a confident answer built on a value that stopped updating hours ago, and that failure is silent by construction — which is the worst kind.

## Traceability

| Requirement | Module | Test cases |
| --- | --- | --- |
| REQ-1 | `sources/weather`, `sources/air`, `core/pipeline`, `fusion` | TC-1.1, TC-1.2, TC-1.3 |
| REQ-2 | `core/reading`, `core/snapshot` | TC-2.1, TC-2.2, TC-2.3 |
| REQ-3 | `sources/*`, `core/snapshot` | TC-3.1, TC-3.2, TC-3.3 |
| REQ-4 | `core/pipeline` | TC-4.1, TC-4.2 |
| REQ-5 | `core/verdict` *(not yet written)* | TC-5.1, TC-5.2, TC-5.3 |
| REQ-6 | `core/log` *(not yet written)* | TC-6.1, TC-6.2, TC-6.3 |

## Test plan

| ID | Level | What it checks | Status |
| --- | --- | --- | --- |
| TC-1.1 | Unit | Each response parser accepts a real payload and rejects malformed, truncated, mistyped and incomplete ones | **Automated** — `pio test -e native` |
| TC-1.2 | Integration | A reading posted by a source task is received by the consumer | Not run |
| TC-1.3 | System | A source holds its cadence across an extended run without drift | Not run |
| TC-2.1 | Unit | Age arithmetic stays correct across the `millis()` rollover | **Automated** |
| TC-2.2 | Integration | The age reported by the consumer matches the elapsed time since acquisition | Not run |
| TC-2.3 | Unit | The snapshot ages each source against its own threshold, independently of the others | **Automated** |
| TC-3.1 | Unit | A failed fetch produces a reading with `valid = false` rather than no reading | Not run |
| TC-3.2 | Integration | A forced HTTP error surfaces downstream as a failure, not as silence | Not run |
| TC-3.3 | Unit | A failure keeps the last good value and is reported as failing — distinguishable from a source never heard from and from one that is merely stale | **Automated** |
| TC-4.1 | Unit | A full queue discards the oldest entry and increments the drop counter | Not run |
| TC-4.2 | Integration | A producer posting faster than the consumer drains never blocks | Not run |
| TC-5.1 | Unit | The verdict engine, across a table of snapshots covering every staleness and missing-input combination | Not started |
| TC-5.2 | Integration | A stale input degrades the live verdict | Not started |
| TC-5.3 | System | Network removed mid-run: the verdict hedges rather than remaining confident | Not started |
| TC-6.1 | Unit | A log record survives a write/read round trip | Not started |
| TC-6.2 | Integration | The log survives power loss mid-write | Not started |
| TC-6.3 | System | Replaying a recorded log reproduces the original verdicts exactly | Not started |

**Four of seventeen are automated.** Up from one of fifteen at v1.0. The number is here so it cannot quietly stay where it is; the eleven that remain are the ones that need a board or a verdict.

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

These exist because the snapshot takes time as a parameter instead of reading a clock. A source can be made forty-five minutes old, or pushed across the 49-day `millis()` wrap, in one line — neither is something to wait for on a board. The one that matters most is **failure-after-good**: the last known value survives a failed fetch and the source is reported as *failing*, which is a different thing from *stale* (old) and from *never seen* (nothing yet). The verdict will need all three to be told apart.

## Known gaps

**No certificate validation.** The HTTPS client calls `setInsecure()`, so the connection is encrypted but the server is not authenticated. Pinning a root CA means shipping a certificate that expires. For public read-only weather data the exposure is low, but it is a real weakness rather than an oversight.

**Nothing has run on hardware.** The firmware builds and the host tests pass; no reading has yet travelled through the queue on real silicon.

**Stack sizes are estimates.** Every task now prints its own high-water mark — the sources after each fetch, the consumer and sink on every tick — but until the firmware has run, the 8 KB allocated for the TLS and HTTP path is a guess with headroom rather than a measurement.
