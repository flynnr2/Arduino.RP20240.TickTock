# Capture timing on RP2040 (arduino-pico): options and recommendation

## Decision (locked)
- Arduino core: **arduino-pico**
- Capture: **Unified PIO edge capture + DMA → RAM ring** for **pendulum edges and GPS PPS** from the start
- Core1 consumes the RAM ring, reconstructs swings, runs PPS discipline, and outputs per-swing samples
- Core0 enriches/logs/serves UI; Core0 never participates in capture

## Output to Core0 / logging
- Core1 produces **one record per swing** using the `SwingRecordV1` schema (raw cycles + PPS alignment + state).
- `pps_new==1` only on the **first swing after a PPS edge**, enabling reconstruction of the PPS series.
See `docs/core1/logging-schema.md`.

## Context

## Unified capture pipeline (pendulum + PPS)
We treat PPS as **just another edge source** and timestamp it through the **same PIO+DMA** pipeline as the pendulum sensor.

### Event word (recommended)
DMA writes a packed word per capture into a RAM ring. A practical 32-bit format:
- `ts`: timestamp in PIO ticks (wrap OK)
- `chg_mask`: which inputs changed (pendulum, PPS)
- `level_bits`: new levels for those inputs

If both inputs change in the same tick, capture emits **one word** with both bits set — collisions become explicit and deterministic.

### Tie handling
Equal timestamps are valid. Downstream code must accept ties and process by `chg_mask` rather than assuming strict ordering.

### Discipline using unified timestamps
Compute `pps_cycles = ts_pps[n] - ts_pps[n-1]` (unsigned wrap). Feed `pps_cycles` into the existing fast/slow smoothing.
Interpret pendulum edge intervals in the same tick domain, applying the discipline scale.

### Resolution guidance
- 1 µs tick gives ~1 ppm quantization on a 1-second PPS interval; often sufficient.
- 0.1 µs (10 MHz) gives more margin; 125 MHz-class ticks are possible but usually unnecessary.
Pick a tick so quantization is comfortably below your timing error budget.

## Acceptance criteria
- `dropped_events == 0` at expected edge rates over multi-hour runs.
- Under load, capture timing variance does not degrade beyond the agreed threshold.
- Any overrun produces explicit counters and is visible via `/status` and logs.

---

## Appendix: Operational constraints & invariants
This appendix documents invariants and constraints that future firmware revisions should preserve.

### Capture tick rate
Pick and document a single PIO capture tick rate (examples):
- **1 MHz**: 1 µs resolution, 32-bit wrap ≈ 4295 s
- **10 MHz**: 0.1 µs resolution, 32-bit wrap ≈ 429 s

The tick rate affects:
- `pps_age_cycles` → `pps_age_ms` conversion
- “plausible” pulse-width sanity checks
- wrap horizons for any timeouts

### Event ties and ordering
If PPS and pendulum edges occur within the same capture tick, ties are valid.
To keep reconstruction deterministic:
- enqueue tied events in a fixed order (recommend: **PPS before pendulum**)
- downstream code must accept `dt == 0` and not assume strict ordering

### Reconstruction failure policy (Core1)
If an unexpected edge pattern is detected while assembling a `SwingRecordV1`:
- set `FLAG_GLITCH`
- resync by discarding events until the next “known good” boundary (e.g., next pendulum rising edge)
- prefer **emitting** a record with `FLAG_GLITCH` rather than skipping records (skips complicate downstream alignment)

### Maximum supported event rate (planning constraint)
Document expected maximum rates so ring sizing and buffer policies remain justified. Example planning target:
- up to **20 swings/s** (fast pendulum case)
- up to **80 edges/s** (including both edges + PPS)

Adjust as needed for your “future clock” envelope; keep a safety margin.

### Hardware edge conditioning assumption
Capture quality depends on edge quality:
- if edges are slow/noisy, true jitter will dominate regardless of capture method
- prefer Schmitt/comparator conditioning if wiring or sensor output is marginal
