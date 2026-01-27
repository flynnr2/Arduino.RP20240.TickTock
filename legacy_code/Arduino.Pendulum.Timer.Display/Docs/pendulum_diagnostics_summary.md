# Pendulum Diagnostics & Analysis Summary (Concise)

This note summarizes the diagnostics, analyses, and visualizations we’ve been running on the Synchronome 2‑second pendulum datasets (gravity arm impulse every ~30s ⇒ 15 swings per impulse cycle), including both **locked-only** (`gps_status==2`) and **all-rows** variants.

---

## Data model assumptions used throughout
- **Period**: `P = tick_block + tick + tock_block + tock`
- **Clock rate**: 16 MHz on Nano Every ⇒ values are in **clock cycles** (sometimes stored in `*_us` fields but treated as cycles).
- **Quality flags**:
  - `gps_status` enum:
    - 0 `NO_PPS`
    - 1 `ACQUIRING`
    - 2 `LOCKED`
    - 3 `HOLDOVER`
    - 4 `BAD_JITTER`
  - `dropped`: counts dropped swings
- **GPS correction fields**:
  - `corr_inst_ppm`: instantaneous correction from latest PPS delta
  - `corr_blend_ppm`: blended fast/slow correction
- **Impulse-cycle expectation**: 2 s period, impulse every 30 s ⇒ **L ≈ 15** swings per impulse cycle.

---

## 1) Pre-analysis data characterization (QC / “what’s in the file?”)
**Computed/Reported**
- Row counts; implied duration from cumulative period.
- Counts of:
  - `gps_status` values (0..4: NO_PPS/ACQUIRING/LOCKED/HOLDOVER/BAD_JITTER)
  - dropped swings (`dropped` sum)
  - **fallouts** from `gps_status==2` (transitions out of 2) and transitions back into 2
- **Hard glitches**: identified and optionally excluded:
  - `period_s < 1.0 s` treated as obvious capture/segmentation glitches
- **Robust outliers**:
  - MAD-based outlier detection on `period_s` (e.g., k≈8×MAD)
  - report extreme outlier **row index** and magnitude (period error)

**Visualizations**
- (Optional) quick time-series panels for period error / asymmetry / correction terms to eyeball bad regions and settling.

---

## 2) Rate / accuracy vs the 2.000000 s target
**Metrics computed**
- Mean period and **period error** (ms) relative to 2.0 s
- Converted to **sec/day**:
  - `sec/day = ((P-2.0)/2.0) * 86400 = period_err_ms * 43.2`
- Computed over multiple windows (as available):
  - Whole run
  - After settling exclusion (e.g., ignore first 12h on multi-day runs)
  - Last 24h / last 3h
  - “first hour vs rest” on short datasets to detect settling transient
- Computed as:
  - **locked-only** (`gps_status==2` and `dropped==0`) for trustworthy rate
  - **all rows** variant (kept rows, excluding only hard glitches) for continuity comparison

**Visualizations**
- Period error vs time:
  - 1-minute binned medians + ~11-minute rolling median
- Comparison plots (locked-only vs all-statuses) where relevant

---

## 3) Tick–tock half-cycle asymmetry & block-time proxy
**Derived signals**
- Half-cycle asymmetry (ms):
  - `A_half = (tick_block + tick) - (tock_block + tock)`
- Block proxy (ms):
  - `block_mean = (tick_block + tock_block)/2`

**Analyses**
- Time-series drift of `A_half` and `block_mean` (binned + rolling medians)
- Stability comparison (does `A_half` drift more/less than period error?)
- Used as a feature for impulse-phase detection and for diagnosing geometry/impulse effects

**Visualizations**
- `A_half` vs time (binned + rolling)
- `block_mean` vs time (binned + rolling)

---

## 4) Mod-15 impulse-cycle analysis (30s impulse cycle)
**Goal**
- Detect whether drift/modulation is concentrated near the impulse vs distributed uniformly.

**Approach**
- Create a swing index and fold by candidate cycle lengths **L=5..60**; score with an ANOVA-style F-statistic on a detrended composite signal.
- Best L consistently identified as **L≈15**.
- Compute phase-folded profiles for:
  - period error
  - `A_half`
  - `block_mean`
- Infer “impulse phase” as the phase with strongest deviation from overall mean (or max |composite|).

**Visualizations**
- F-statistic score vs L (cycle length search)
- Phase-folded mean profiles with SEM error bars:
  - period error vs phase (mod 15)
  - `A_half` vs phase
  - `block_mean` vs phase

---

## 5) “Impulse-geometry modulation” vs true mean drift (exclude impulse phase)
**Goal**
- Determine how much apparent drift is due to impulse-phase waveform changes vs true mean period change.

**Approach**
- Identify inferred impulse phase (e.g., phase ≈ 2 mod 15).
- Recompute rate metrics and drift curves for:
  - all phases
  - **exclude impulse phase**
  - impulse-phase-only
- Compare drift curves and daily means:
  - (no-impulse rolling) − (all-phases rolling)
- Detect regime changes where impulse-phase behavior flips and can masquerade as drift.

**Visualizations**
- Drift comparison: all phases vs exclude impulse phase
- Difference series: (no-impulse) − (all-phases)
- Daily mean comparisons
- Phase-fold regime comparison (early vs last 24h, etc.)

---

## 6) Environmental coupling (temperature / humidity / pressure)
**Data handling**
- Use locked-only, post-settling portion when available.
- 1-minute median binning for robustness.

**Analyses**
- Univariate linear fits: period error (ms) vs each env variable; report slope and R²
- Multivariate regression using all three env variables; report coefficients and R²
- Convert slopes to sec/day per unit (°C, %RH, hPa)
- Compare predicted period swing from observed env ranges vs actual observed period range
- **Detrended (high-pass) analysis**:
  - subtract ~6-hour rolling median from both period and env variables
  - recompute correlations/regressions to separate slow co-drift from short-timescale sensitivity

**Visualizations**
- Scatter: period error vs each env variable (raw)
- Scatter: detrended period error vs detrended env variable (6h high-pass)
- Time series of env variables and period error (binned + rolling)

---

## 7) Lagged response & distributed-lag modeling (environment → period)
**Lag check (cross-correlation)**
- Compute correlation vs lag (±12h), with sign convention:
  - +lag ⇒ environment leads period
- Applied to detrended (6h high-pass) 1-minute series.

**Distributed lag model (DLM)**
- Ridge regression using env history over 0..240 minutes (step ~10 minutes):
  - y(t) ≈ Σ w_k x(t−lag_k) + intercept
- Train/validation split (e.g., last 30% holdout) to estimate predictive power.
- Summarize impulse response curves:
  - peak lag, center-of-mass of |weights|, cumulative step response.

**Visualizations**
- Lag-correlation curves per variable
- DLM impulse response curves per variable (weights vs lag)
- Validation R² comparison (temp-only / hum-only / press-only / all three)

---

## 8) Allan deviation / stability (frequency-domain time stability)
**Approach**
- Use 1-minute binned period series to compute fractional frequency:
  - f = 1/P, y = (f − f0)/f0 with f0 = 0.5 Hz for 2 s period
- Compute **overlapping Allan deviation** σy(τ) over multiple τ values.
- Compare:
  - locked-only vs all-statuses
  - effect of mixed-status minutes / lock fraction

**Visualizations**
- Allan deviation log-log plot
- Locked vs all-status Allan comparison plot
- Scatter: per-minute period error vs lock fraction (to show contamination)

---

## 9) GPS status effects on measured period variability
**Per-row distributions**
- Side-by-side box plots of period error by `gps_status` (0..4), fliers hidden.
- Summaries: median, IQR, 5–95% ranges.

**Minute-level distributions (more diagnostic)**
- Compute per-minute median period error.
- Assign minute to a status only if that status dominates the minute (e.g., ≥80% of swings).
- Box plots of per-minute medians by status (0..4).

**Visualizations**
- Box plots: per-row period error by status (0..4)
- Box plots: minute-level median period error by status (dominance threshold)

---

## 10) Code-path interpretation of `corr_inst_ppm` vs `corr_blend_ppm`
**Key finding**
- During **ACQUIRING**, forcing the active PPS denominator fully to the **fast** track can inject PPS jitter directly into the scaling of every swing → increased variability in `gps_status=1` and transition-adjacent minutes.
- In HOLDOVER/NO_PPS, `pps_delta_active` may remain constant, but transitions and mixed-status minutes can still widen distributions.

**Practical interpretation**
- For rate estimation, locked-only remains the trustworthy basis.
- For diagnosis, keep all rows but annotate with a clear `gps_state` and PPS freshness.

---

## 11) Recommended logging architecture (for transparency & post-processing)
**Preferred: log raw and do cleaning/disciplining in post**

> Note: `gps_status` is now a full 5-state enum (NO_PPS/ACQUIRING/LOCKED/HOLDOVER/BAD_JITTER) and can serve as the primary state field (i.e., you may not need a separate `gps_state`).
- Log **raw swing cycles** (tick/tock/block components) + minimal PPS raw fields.
- Keep a combined record per swing, but include freshness and PPS-series extraction flags.

**Preferred single-line combined record fields**
- Swing raw (cycles): `tick_block_cycles, tick_cycles, tock_block_cycles, tock_cycles`
- Alignment/epoch: `swing_id, pps_id, pps_age_cycles` (or `pps_age_ms`)
- PPS raw (meaningful only on new PPS): `pps_interval_cycles_raw, pps_new`
- State/flags: `gps_status, flags` (bitfield)
- Optional: derived `gps_status` (0/1/2) for convenience, but treat `gps_state` as authoritative.

**Recommended `gps_status` enum (authoritative)**

```cpp
enum GpsStatus : uint8_t {
  NO_PPS     = 0,  // never seen PPS or PPS absent beyond horizon; no valid epoch
  ACQUIRING  = 1,  // PPS present but not yet stable enough to trust
  LOCKED     = 2,  // PPS present and stable; trusted regime
  HOLDOVER   = 3,  // PPS absent, but last-good scale exists and is being held
  BAD_JITTER = 4,  // PPS present but too noisy/out-of-family to trust
};
```

**Precedence / interpretation**: if PPS absent ⇒ HOLDOVER (if last-good exists) else NO_PPS; if PPS present ⇒ BAD_JITTER / ACQUIRING / LOCKED.


---

## Delivered plot bundles (zips) during analysis
- `pendulum_analysis_allrows_plots.zip`: full main analysis keeping all rows (excluding hard glitches)
- `pendulum_impulse_exclusion_plots.zip`: impulse-phase exclusion vs all-phases decomposition
- `pendulum_environment_relationship_plots.zip`: env scatter/time series + detrended views
- `pendulum_env_lag_analysis_plots.zip`: lag correlation curves
- `pendulum_distributed_lag_model_plots.zip`: distributed-lag impulse response + validation R²
- `pendulum_period_variability_by_gps_status.zip`: per-row status box plots
- `pendulum_period_variability_by_gps_status_minutelevel.zip`: minute-level status box plots
- `short_dataset_overview_plots.zip`: short-run overview (settling/period/asym/corr)

---
