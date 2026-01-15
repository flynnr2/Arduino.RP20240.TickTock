# Core0 Processing (DSP / derived values)

This document defines Core0’s real-time processing rules. It is the “DSP heart” of the system:
- stable-first scale estimation (cycles → seconds) without chasing jitter
- sample usability gating for stats/UI
- rolling window definitions and robust statistics
- overflow/wrap rules and sanity checks

Core0 must never feed these derived values back into Core1 in v1.

---

## 1) Timebase and wrap policy

### Raw timebase
- Core1 emits all measurements in `*_cycles` (single tick domain).
- Wrap is handled via unsigned subtraction:

```c
uint32_t dt = (uint32_t)(t2 - t1);
```

### Rules
- Never compare timestamps across wrap using signed math.
- All deltas are computed using unsigned wrap-safe subtraction.
- The conversion scale is applied only for *derived* seconds values.

---

## 2) Stable-first scale (`pps_cycles_last_good`)

Core0 maintains a last-good PPS scale in cycles per second:

- `pps_cycles_last_good` is updated **only** when:
  - `pps_new == 1`
  - `gps_state == LOCKED`
  - the PPS interval passes quality gates (see below)

Policy:
- `LOCKED`: update slowly and robustly (do not chase short-term twitch)
- `ACQUIRING`: hold last-good (do not chase)
- `BAD_JITTER`: hold last-good (do not chase)
- `HOLDOVER`: continue using last-good
- `NO_PPS` with no last-good: derived seconds are invalid (cycles remain valid)

Recommended estimator (choose one and keep stable):
- **Median-of-N** PPS intervals (e.g. N=5…15) computed from accepted `pps_interval_cycles_raw`, OR
- **Slow EWMA** (small alpha) applied only to accepted PPS intervals.

The estimator should be robust to single-sample spikes.

---

## 3) PPS quality gates (Core0 side)

Core1 already emits `gps_state` and flags. Core0 additionally applies a simple acceptance test to PPS intervals before updating the scale.

Minimum recommended rules for a PPS sample to be “accepted”:
- `pps_new == 1` and `pps_interval_cycles_raw > 0`
- Not flagged as outlier by Core1 (i.e., `FLAG_PPS_OUTLIER` not set on the surrounding swings), and
- Interval is within a reasonable band around expected `clk_sys` cycles per second.

Example band (configurable):
- `abs(ppsi - pps_cycles_last_good) <= max( max_ppm_step * pps_cycles_last_good / 1e6, min_step_cycles )`

Where:
- `max_ppm_step` is a conservative threshold (stable-first), e.g. 50–200 ppm.
- `min_step_cycles` avoids being too strict at startup.

If rejected:
- do not update the scale
- set/accumulate `FLAG_CLAMP` or a Core0 counter (optional)
- allow `gps_state` to remain `LOCKED` if Core1 says so; this gate is a Core0 “don’t chase” rule.

---

## 4) Sample usability gating (`usable_for_stats`)

Core0 should define and use a derived boolean `usable_for_stats`:
- This is **not** a new `gps_state`, just a local rule set to prevent bad samples contaminating stats and UI.

Recommended default:
- `usable_for_stats = true` when:
  - `gps_state` is `LOCKED` or `HOLDOVER`, AND
  - none of the following flags are set:
    - `FLAG_GLITCH`, `FLAG_DROPPED`, `FLAG_RING_OVERFLOW`
- Otherwise false.

Policy:
- OLED and `/stats` should compute rolling statistics primarily from `usable_for_stats` samples.
- Raw logging remains unaffected (raw is authoritative regardless).

---

## 5) Rolling windows

Core0 maintains rolling stats in RAM for:
- OLED display (fast refresh)
- `/stats` endpoint
- optional stats CSV snapshots (lower cadence)

Define windows explicitly:
- Window A: **short window** (e.g. last 30–120 swings) for OLED responsiveness
- Window B: **long window** (e.g. last 10–60 minutes worth of swings) for stability view

A window can be defined by:
- **N swings** (preferred and simple), and optionally
- an approximate **window_ms** derived from `uptime_ms` differences.

Stats should be computed from samples where `usable_for_stats == true`.

---

## 6) Robust statistics (recommended defaults)

For pendulums, occasional sensor glitches are more common than “true” distribution changes. Robust metrics behave better:

Compute (per window):
- `period_mean_s` (mean of accepted samples)
- `period_median_s` (optional but useful)
- `period_mad_s` (median absolute deviation)
- `period_std_s` (optional; can be 0 if not computed)

Recommendation:
- Prefer **median + MAD** for OLED “variability” display.
- Keep mean/std for compatibility with existing analysis expectations.

---

## 7) Sanity checks & self diagnostics

Core0 should compute lightweight checks and surface them via `/status`, OLED health page, and optionally the stats CSV:
- PPS sanity:
  - track min/max accepted `pps_interval_cycles_raw`
  - count rejected PPS samples (gates)
- Swing topology:
  - `swing_cycles = tick_block + tick + tock_block + tock`
  - pulse widths `tick_cycles`, `tock_cycles` in plausible ranges (config)
  - increment counters and set `FLAG_GLITCH` when pattern breaks

These checks help detect wiring/noise regressions quickly.

---

## 8) Backpressure and degraded mode

If Core0 cannot keep up (queue pressure rises, `FLAG_RING_OVERFLOW` occurs):
- Maintain counters and expose a “degraded mode” state
- Reduce optional work in priority order:
  1. lower OLED refresh rate
  2. reduce stats compute work / window sizes
  3. reduce stats CSV cadence
  4. reduce SD flush frequency (keep bounded)
- Never slow Core1 capture.

---

## 9) Optional: online stability indicator

Optionally compute a lightweight stability indicator for OLED:
- e.g., Allan deviation proxy at a few τ in “swings” space
- or simply show MAD scaled to ppm around target period

This is convenience only; full stability analysis remains post-processing.

---

## Appendix: Operational envelope and glitch visibility
### Operational envelope (planning)
Rolling windows, SD buffering, and `/latest` update rates should be tested against a documented target envelope (e.g., 20 swings/s).
If you raise the envelope, revisit:
- queue/ring sizes
- SD flush cadence
- OLED refresh limits
- HTTP serialization frequency

### Glitch visibility
When `FLAG_GLITCH` is set, treat the sample as `usable_for_stats=false` by default and increment a visible counter in `/status`.
