# TODO suggestions from ChatGPT5

## OTHER THOUGHTS

| Functionality                              | Notes / Subtleties                                         | MicroSet | PICTock                          |
|--------------------------------------------|-------------------------------------------------------------|----------|-----------------------------------|
| **Beat rate measurement** (BPH, sec/beat)  | Core function for all models                               | ✅       | ✅                                 |
| **Beat error measurement**                 | Difference between left/right swing intervals              | ✅       | ✅ *(less precise on basic builds)*|
| **Pendulum period measurement**            | Measures individual swing duration                         | ✅       | ✅                                 |
| **Rate deviation (sec/day)**               | Shows how far clock is from nominal rate                   | ✅       | ✅                                 |
| **Strike counting**                        | Counts chime/strike events per hour/day                    | ✅       | ❌                                 |
| **Elapsed time measurement**               | Stopwatch-like mode for events                             | ✅       | ❌                                 |
| **Pendulum length calculation**            | Calculates theoretical length from period                  | ✅       | ❌                                 |
| **Tachometer mode**                        | Measures rotations/minute for other mechanisms             | ✅       | ❌                                 |
| **Width of tick measurement**              | Duration of acoustic impulse                               | ✅       | ❌                                 |
| **Light/dark detection**                   | Measures optical signal duty cycle                         | ✅       | ❌                                 |
| **Temperature logging**                    | Uses optional sensor; can compensate rate                  | ✅       | ❌                                 |
| **Barometric pressure logging**            | Uses optional sensor; can compensate rate                  | ✅       | ❌                                 |
| **GPS timebase**                           | High-precision reference clock                             | ✅       | ❌                                 |
| **Accutron/tuning fork measurement**       | Measures frequency of tuning fork watches                  | ✅       | ❌                                 |
| **Running average / stat. smoothing**      | Averages over set interval for stability                   | ✅       | ✅ *(basic averaging)*             |
| **Data logging to PC**                     | Stores long-term measurements for analysis                 | ✅       | ⚠️ *(limited/manual capture)*     |
| **Graphical display of results**           | PC software shows timing graphs                            | ✅       | ❌                                 |
| **LCD/LED onboard display**                | Shows live readings                                        | ✅       | ✅                                 |

| Drive / Impulse type                                                        | Dominant disturbance you’ll see                                             | What to measure & windowing                                              | GPS blending / loop filter                                             | Extra tricks that help                                                                 |
|------------------------------------------------------------------------------|------------------------------------------------------------------------------|---------------------------------------------------------------------------|-------------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| **Plain mainspring (barrel), no fusee**                                      | Slow torque roll-off over hours/days → slow frequency drift; some barrel periodicity (once per turn) | Average period over 30–120 s windows; track frequency + drift (aging)    | I-only loop with very long τ (hours) or 2-state Kalman (freq + drift)   | Very slow feed-forward vs barrel angle if sensed; notch gear periodicity if visible   |
| **Mainspring + fusee**                                                       | Flattened torque → less secular drift; residual gear ripple                  | Shorter windows OK (10–60 s)                                              | Low-bandwidth PI/PLL or I-only; Kalman optional                         | Notch or median-of-means to suppress known gear periodicities                         |
| **Weight drive (gravity)**                                                   | Constant torque; gear-train ripple, escapement asymmetry, temp effects       | Windows spanning several gear periods (30–120 s)                         | PI/PLL with very low bandwidth; I-only also works                       | Synchronous averaging or notch filters for gear ripple; temp feed-forward              |
| **Electromagnetic impulse every swing** (one-sided or timing-biased)         | Cycle-by-cycle phase pulling; possible AM→FM; line-frequency noise           | Ignore single swings; pair odd/even swings; window 20–100 swings          | 2nd-order digital PLL (milli-Hz BW) or Kalman (phase + freq states)     | Keep impulses symmetric; separate amplitude loop; cap per-window correction           |
| **Periodic free-swing with 30 s kick** (Synchronome-style)                   | Stepwise top-ups → small periodic phase modulation at 30 s                   | Average over multiples of 30 s; median-of-means across 2–3 such blocks    | I-only on frequency, τ in hours; optionally subtract known periodic term | Synchronous detection at 30 s to cancel deterministic phase wiggle                     |
| **Hipp toggle / GPO 36** (random-interval impulses when amplitude falls)     | State-dependent, non-uniform impulses; amplitude-frequency coupling          | Long windows (1–5 min) with robust estimators; track amplitude            | Kalman with states [phase, freq, amplitude]; GPS weight very low        | Close loop on amplitude separately to linearize frequency                             |
| **Electro clocks that re-tension a spring periodically** (E. Howard, Rempe)  | Remontoire-like sawtooth drive every N sec/min → periodic FM                  | Average over integer multiples of tensioning period; store event phase    | PI/PLL with notch at remontoire period (and 2nd harmonic)               | Timestamp tensioning events; exclude short windows around them                         |

---

### General pattern that works well (and maps nicely to a Nano Every GPSDO-style setup)

1. **Measure smartly (front-end smoothing)**  
   - Timestamp every zero-crossing with hardware capture (TCB) against a disciplined counter.  
   - Form robust windowed estimates of period/frequency: median-of-means over M blocks, each of K swings (e.g., 3×20 swings).  
   - Where impulses are periodic, choose windows that are integer multiples of that period.

2. **Split the control loops**  
   - **Amplitude loop** (fast, local): keeps swing energy constant with symmetric impulses → suppress AM→FM.  
   - **Rate/phase loop** (slow, GPS-blended): I-only or very low-BW PLL, time constant in hours for good oscillators, tens of minutes if the drive is less stable.

3. **Model what’s deterministic**  
   - If your mechanism creates a known periodic disturbance (30 s kick, remontoire), either synchronize your averaging to it or estimate and subtract that component (a single sine/cosine at the interval works well).

4. **Use Kalman when the drive is state-dependent or impulsive**  
   - Hipp/similar designs benefit from a small Kalman filter with states like `[phase, frequency, frequency_drift, amplitude]`.  
   - Give GPS a tiny measurement weight (reflecting PPS jitter); let the filter trust the pendulum short-term and GPS long-term.

---

### Nano Every (ATmega4809) implementation sketch

- **Capture:** TCB1 input capture for PPS; TCB2 or event system for IR sensor.  
- **Clock:** Use TCB0 as a free-running tick counter; calibrate it against GPS PPS with a multi-minute EMA so your timestamps are on a quiet local timescale.  
- **Windows:** For steady drives: 30–120 s; for impulsive or periodic drives: multiples of the impulse period; for Hipp: 1–5 min robust windows.  
- **Loop constants:** Start with I-only where `Ki ≈ (1e-6 … 5e-6)` per 30 s update (≈ hours τ). Shorten if your drive is obviously wandering.  
- **Safety:** Deadband of ±0.1–0.3 ppm on the rate loop so you don’t hunt GPS noise; cap per-update phase correction to a few ms.

---

### Quick chooser

- **Cleanest, constant-force drives (weight, good fusee):** low-BW PI/PLL or I-only; short windows OK.  
- **Per-swing electromagnetic:** PLL or Kalman, very low bandwidth, strict symmetry + amplitude loop.  
- **Periodic kick (Synchronome/remontoire):** synchronize windows; I-only + notch/synchronous cancellation.  
- **Hipp/random kick:** Kalman with amplitude state + long, robust averaging; very gentle GPS weight.

# Universal Pendulum Timer Strategy

Design goal: a timer that can handle **any** pendulum/drive type, blending a quiet short-term reference (pendulum) with a long-term stable reference (GPS PPS).

---

## High-Level Architecture

1. **Sensing & Timestamping**
   - IR/optical gate at mid-arc; capture zero-crossings (both directions).
   - Hardware timebase: free-running counter (TCB0) disciplined by GPS PPS using a long-τ EMA so local ticks are quiet but correct long-term.
   - Record: timestamp, swing direction, peak amplitude proxy (e.g., max light change), and any "event pins" (e.g., remontoire/kick coil).

2. **Front-End Robust Estimator (per update window)**
   - Build period estimates from consecutive crossings.
   - Use **median-of-means (MoM)**: split K swings into M blocks, mean each block, then median across blocks.
   - Output: mean period \(\bar T\), beat error, variance, amplitude stats, residuals \(r_i = T_i - \bar T\).

3. **Automatic Disturbance Classifier**
   - **Odd/even test:** \( \Delta_{oe} = |\bar T_{\text{odd}} - \bar T_{\text{even}}| \) → flags per-swing asymmetry.
   - **Periodogram/autocorr of residuals:** peaks at 30 s, 60 s, etc. → periodic kick/remontoire.
   - **Impulse detector:** outlier rate & amplitude correlation → Hipp/random kick.
   - **Drift slope:** linear trend of \(\bar T\) → mainspring roll-off.
   - Produces a **profile** used to select presets.

4. **GPS Blending Timescale**
   - Maintain phase/frequency estimate of pendulum vs local seconds.
   - Blend with GPS using **very low bandwidth** (τ from noise observed):
     - Constant-force → τ ≈ 1–3 h
     - Fusee → τ ≈ 30–90 min
     - Periodic/remontoire or per-swing EM → τ ≈ 2–6 h (plus notches)
     - Hipp/random → τ ≈ 3–8 h (and robust estimators)

5. **Estimator/Control Core (Selectable)**
   - **Mode A:** I-only frequency integrator (default).
   - **Mode B:** 2nd-order digital PLL for per-swing EM or explicit phase control.
   - **Mode C:** Compact **Kalman** \([phase, freq, drift, amplitude]\) for Hipp/random or amplitude-coupled drives.
   - All modes take the same MoM output, so you can swap at runtime.

6. **Adaptive Suppression of Known Disturbances**
   - **Synchronous averaging:** window length = integer multiple of detected periodicities.
   - **Auto-notch:** track strongest deterministic periods and suppress them.
   - **Odd/even de-bias:** average odd/even periods when per-swing pull is detected.
   - **Gating:** exclude short intervals around detected tensioning/impulse events.

7. **Outputs & Diagnostics**
   - Rate (ppm, s/day), beat error, Allan-like stability over τ = 10 s, 100 s, 1000 s.
   - Disturbance report: detected periods, odd/even imbalance, outlier rate.
   - Flags: “Periodic-kick”, “Hipp-like”, “Gear-ripple”, “Mainspring drift”.

---

## Presets & Filters (Auto-Selected by Classifier)

| Profile Detected                                  | Windowing (K swings)         | GPS Blend τ   | Filter Core               | Extra Suppression                                 |
|---------------------------------------------------|------------------------------|---------------|---------------------------|---------------------------------------------------|
| **Constant-force / weight / good fusee**          | 30–120 s MoM                 | 1–3 h         | I-only                    | Optional notch at gear ripple                     |
| **Mainspring drift (no fusee)**                   | 60–180 s MoM                 | 2–4 h         | I-only + drift term       | Linear drift removal over run                     |
| **Per-swing EM asymmetry**                        | 20–100 swings MoM + odd/even | 3–6 h         | 2nd-order PLL             | Odd/even pairing; cap per-update phase correction |
| **Periodic kick / remontoire (30 s / 60 s)**      | N×period (N=2–3) windows     | 2–6 h         | I-only                    | Synchronous averaging + auto-notch                |
| **Hipp / random impulses**                        | 1–5 min robust MoM           | 3–8 h         | Kalman (phase,freq,amp)   | Amplitude tracking; gating around impulses        |

---

## Practical Numbers (Starting Points)

- **MoM:** M=3 blocks × K=20 swings (≈20–60 s).  
  For Hipp: M=3 × K=60–100 swings.
- **Deadband:** Ignore |freq error| < 0.1–0.3 ppm to prevent hunting.
- **Per-update cap:** Limit phase correction to 1–3 ms per window.
- **PLL (Mode B):** Loop BW 0.5–2 mHz, ζ≈0.7.
- **Kalman (Mode C) States:**  
  \([ \phi, f, f_{\text{drift}}, A ]\) with Q from recent ADEV; R from PPS scatter over 5–15 min EMA.

---

## Hardware/Firmware Tips (Nano Every)

- **Capture:** TCB1 for PPS; TCB2 or EVSYS for IR sensor.  
- **Clock:** TCB0 free-running; discipline vs PPS with multi-minute EMA.  
- **Timestamps:** No stepping counter — adjust conversion gain only.  
- **Events:** Timestamp remontoire/coil events if available.  
- **Math:** Use fixed-point (e.g., Q32.32) for speed and stability.

---

## Why This Works

- **Classifier** → avoids using the wrong estimator for the drive type.  
- **Robust front-end** → filters out deterministic disturbances before GPS blending.  
- **Slow GPS blend** → keeps short-term pendulum quietness, long-term GPS accuracy.

## Drive-Type Disturbance Reference

| Drive / Impulse type                                                        | Dominant disturbance you’ll see                                             | What to measure & windowing                                              | GPS blending / loop filter                                             | Extra tricks that help                                                                 |
|------------------------------------------------------------------------------|------------------------------------------------------------------------------|---------------------------------------------------------------------------|-------------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| **Plain mainspring (barrel), no fusee**                                      | Slow torque roll-off over hours/days → slow frequency drift; some barrel periodicity (once per turn) | Average period over 30–120 s windows; track frequency + drift (aging)    | I-only loop with very long τ (hours) or 2-state Kalman (freq + drift)   | Very slow feed-forward vs barrel angle if sensed; notch gear periodicity if visible   |
| **Mainspring + fusee**                                                       | Flattened torque → less secular drift; residual gear ripple                  | Shorter windows OK (10–60 s)                                              | Low-bandwidth PI/PLL or I-only; Kalman optional                         | Notch or median-of-means to suppress known gear periodicities                         |
| **Weight drive (gravity)**                                                   | Constant torque; gear-train ripple, escapement asymmetry, temp effects       | Windows spanning several gear periods (30–120 s)                          | PI/PLL with very low bandwidth; I-only also works                       | Synchronous averaging or notch filters for gear ripple; temp feed-forward              |
| **Electromagnetic impulse every swing** (one-sided or timing-biased)         | Cycle-by-cycle phase pulling; possible AM→FM; line-frequency noise           | Ignore single swings; pair odd/even swings; window 20–100 swings          | 2nd-order digital PLL (milli-Hz BW) or Kalman (phase + freq states)     | Keep impulses symmetric; separate amplitude loop; cap per-window correction            |
| **Periodic free-swing with 30 s kick** (Synchronome-style)                   | Stepwise top-ups → small periodic phase modulation at 30 s                   | Average over multiples of 30 s; median-of-means across 2–3 such blocks    | I-only on frequency, τ in hours; optionally subtract known periodic term | Synchronous detection at 30 s to cancel deterministic phase wiggle                     |
| **Hipp toggle / GPO 36** (random-interval impulses when amplitude falls)     | State-dependent, non-uniform impulses; amplitude-frequency coupling          | Long windows (1–5 min) with robust estimators; track amplitude            | Kalman with states [phase, freq, amplitude]; GPS weight very low        | Close loop on amplitude separately to linearize frequency                             |
| **Electro clocks that re-tension a spring periodically** (E. Howard, Rempe)  | Remontoire-like sawtooth drive every N sec/min → periodic FM                 | Average over integer multiples of tensioning period; store event phase    | PI/PLL with notch at remontoire period (and 2nd harmonic)               | Timestamp tensioning events; exclude short windows around them                         |

## Measuring Amplitude and Q

---

### a) Measuring Amplitude

| Sensor Type                  | Method                                                                                             | Notes                                                                                           |
|------------------------------|----------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------|
| Optical (beam-break / reflective) | Measure time between a defined light threshold on each side of mid-arc. This “half-period arc time” can be mapped to angular displacement. | Works best if you position the beam off-center so the beam is only interrupted over a known arc. |
| Magnetic pickup              | Use induced voltage shape to determine when bob is near coil center and when it leaves.            | Requires good coil alignment; signal can be noisy.                                              |
| Hall effect sensor           | Place sensor offset from midline; detect time bob passes at both offsets.                         | Calibrate distance to angle.                                                                    |
| High-speed photodiode array  | Direct measurement of position vs. time.                                                           | Overkill unless you need very precise motion profiling.                                         |

**Calculation:**
1. For a simple optical gate at mid-arc:
   - Measure the time inside the beam for each swing.
   - Knowing bob speed near the center (from period), convert to distance traveled in the beam.
   - Use geometry to compute maximum angular displacement.
2. Store amplitude history alongside period data.

---

### b) Measuring Q

**Definition:**  
The quality factor (Q) measures how underdamped the pendulum is — the ratio of stored energy to energy lost per cycle.

**Direct decay method (best accuracy):**
1. Let the pendulum swing freely (no drive) until amplitude decays noticeably.
2. Record amplitude per swing: A1, A2, ...
3. Fit an exponential decay:  
   A_n = A_0 × exp(-n × δ)  
   where δ is the logarithmic decrement.
4. Estimate Q ≈ π / δ.

**Logarithmic decrement from two amplitudes:**
- Choose A_n and A_(n+k) separated by k cycles:  
  δ = (1/k) × ln(A_n / A_(n+k))  
  Q ≈ π / δ.  
- Larger k reduces measurement noise.

**Continuous driving method (non-intrusive, in-service):**
- If drive energy per impulse and steady-state amplitude are known:  
  Q ≈ (Energy stored / Energy lost per cycle) × 2π.  
- For small damping, Q is roughly proportional to stored energy divided by average drive energy.

---

### Why Amplitude & Q Matter for Timing

- **Amplitude–Period coupling**:  
  Real pendulums exhibit “circular error” — period increases slightly with amplitude.  
  This means period drift can come purely from amplitude variation.
- **Q–Stability**:
  - High Q → better short-term stability but more sensitivity to slow environmental drift.
  - Low Q → damps quickly after disturbances but more sensitive to drive irregularities.
- **Control loops**:
  - An inner amplitude control loop can keep amplitude constant.
  - Q monitoring can reveal mechanical issues like friction or dirt.

---

### Implementation in a Nano Every Pendulum Timer

- **Amplitude**:
  - Timestamp both entry and exit edges of the optical signal per swing.
  - Convert to angular displacement via calibration.
- **Q**:
  - Offline Q: Disable drive and log amplitude decay.
  - Online Q: Estimate from drive pulse energy and amplitude history.
- Store both amplitude and Q alongside period data so that GPSDO disciplining can account for circular error and damping changes.

## Differentiating "Out of Beat" vs "Off-Centre Sensor"

### Core idea
- **Out of beat (mechanical):** asymmetry comes from the escapement. It is **largely constant** with amplitude (within small-angle limits) and **doesn’t flip** if you move the sensor.
- **Off-centre sensor (measurement):** asymmetry comes from where/how you detect “center.” It **changes with amplitude** in a predictable way and **flips sign** if you mirror/rotate the sensor.

---

### Quick Tests (with expected outcomes)

| Test                                   | How to perform                                                                 | If it’s **Out of Beat**                                   | If it’s **Off-Centre Sensor**                                  |
|----------------------------------------|---------------------------------------------------------------------------------|------------------------------------------------------------|-----------------------------------------------------------------|
| **Amplitude sweep**                    | Vary amplitude (or log during natural decay) and plot beat error vs amplitude. | Beat error ~ **constant** vs amplitude.                    | Beat error scales ~ **1/amplitude** (gets smaller as amplitude grows). |
| **Sensor swap / mirror**               | Move sensor to the mirrored position or rotate the bracket 180°.                | **Same sign/magnitude** (within noise).                    | **Sign flips** (and/or magnitude changes predictably).          |
| **Dual-sensor symmetry**               | Use two sensors placed symmetrically about physical center; compute mid-time = average of the two crossing times. | Beat error visible **even with mid-time** reference.       | Beat error **vanishes** (≈0) when using mid-time reference.     |
| **Acoustic tick correlation**          | Timestamp ticks acoustically; compare tick timing around the **kinematic** mid-plane (from optics). | Ticks are **not equidistant** about true mid-plane.        | Ticks become **equidistant** once you re-center the optical reference. |
| **Free-swing baseline**                | Temporarily disengage escapement (or let it miss) and log half-periods.         | **Zero** beat error when free; returns when escapement re-engages → indicates escapement geometry. | **Non-zero** beat error even free-swinging → it’s the sensor geometry. |
| **Odd/Even pairing**                   | Compare odd vs even half-periods; then offset the sensor slightly and repeat.   | Pattern **unchanged** by small sensor nudges.              | Pattern **tracks** the sensor offset; can be nulled by centering. |

---

### Practical Implementation (universal timer)

1. **Front-end measurements**
   - Timestamp both edges of the optical gate; derive **half-periods** (left/right), **mid-time**, and an **amplitude proxy** (time-in-beam or threshold spread).
   - (Optional) Add a 2nd optical gate symmetrically placed; compute **mid-time = (t₁ + t₂)/2** each swing.

2. **Classification logic**
   - **Amplitude test:** Fit beat-error vs amplitude. If slope ≈ proportional to 1/A → **sensor offset**. If flat → **out of beat**.
   - **Sensor flip test:** Prompt user to flip/mirror sensor; if sign flips → **sensor offset**.
   - **Dual-sensor check:** If single-sensor beat error ≠ 0 but dual-sensor mid-time beat error ≈ 0 → **sensor offset**.

3. **Auto-correction (software)**
   - Estimate the optical center offset (in time) from amplitude scaling or dual-sensor mid-time.
   - **Corrected beat error** = measured beat error − optical-offset term.
   - Report both: **“raw”** and **“geometry-corrected”** beat error.

4. **Guidance to the user**
   - If classified **out of beat**: advise pallet/crutch adjustment; show live numeric beat error and a centering bar.
   - If **sensor offset**: show “Center Sensor” wizard: nudge until **entry time ≈ exit time** at the same amplitude, or until dual-sensor mid-time beat error ≈ 0.

---

### Rules of thumb (why this works)
- Sensor offset introduces a **kinematic time bias** around the true center. As amplitude increases, center velocity increases, so the **apparent beat error shrinks ~ 1/A**. Real out-of-beat is **geometric in the escapement** and stays roughly **constant** with amplitude (small-angle regime).
- Mirroring the sensor flips a **measurement bias**, not a mechanical bias.
- A **dual-sensor mid-time** is essentially immune to single-sensor centering errors and exposes the real escapement asymmetry.

---

### Minimal signals to implement
- One optical gate (edge times) → half-periods, amplitude proxy, raw beat error.
- (Recommended) **Second symmetric gate** → mid-time (geometry-free beat error).
- (Nice to have) Acoustic mic → tick timing cross-check.

# Universal Pendulum Timer Strategy

Design goal: a timer that can handle **any** pendulum/drive type, blending a quiet short-term reference (pendulum) with a long-term stable reference (GPS PPS).

---

## High-Level Architecture

1. **Sensing & Timestamping**
   - IR/optical gate at mid-arc; capture zero-crossings (both directions).
   - Hardware timebase: free-running counter (TCB0) disciplined by GPS PPS using a long-τ EMA so local ticks are quiet but correct long-term.
   - Record: timestamp, swing direction, peak amplitude proxy (e.g., max light change), and any "event pins" (e.g., remontoire/kick coil).

2. **Front-End Robust Estimator (per update window)**
   - Build period estimates from consecutive crossings.
   - Use **median-of-means (MoM)**: split K swings into M blocks, mean each block, then median across blocks.
   - Output: mean period, beat error, variance, amplitude stats, residuals.

3. **Automatic Disturbance Classifier**
   - Odd/even test → flags per-swing asymmetry.
   - Periodogram/autocorr of residuals → detects periodic kicks or gear ripple.
   - Impulse detector → outlier rate & amplitude correlation.
   - Drift slope → mainspring roll-off.
   - **Beat-error origin test** (see section below).
   - Produces a **profile** used to select presets.

4. **GPS Blending Timescale**
   - Maintain phase/frequency estimate of pendulum vs local seconds.
   - Blend with GPS using **very low bandwidth** (τ from noise observed):
     - Constant-force → τ ≈ 1–3 h
     - Fusee → τ ≈ 30–90 min
     - Periodic/remontoire or per-swing EM → τ ≈ 2–6 h (plus notches)
     - Hipp/random → τ ≈ 3–8 h (and robust estimators)

5. **Estimator/Control Core (Selectable)**
   - Mode A: I-only frequency integrator (default).
   - Mode B: 2nd-order digital PLL for per-swing EM or explicit phase control.
   - Mode C: Compact Kalman [phase, freq, drift, amplitude] for Hipp/random or amplitude-coupled drives.
   - All modes take the same MoM output, so you can swap at runtime.

6. **Adaptive Suppression of Known Disturbances**
   - Synchronous averaging: window length = integer multiple of detected periodicities.
   - Auto-notch: track strongest deterministic periods and suppress them.
   - Odd/even de-bias: average odd/even periods when per-swing pull is detected.
   - Gating: exclude short intervals around detected tensioning/impulse events.

7. **Outputs & Diagnostics**
   - Rate (ppm, s/day), beat error, Allan-like stability over τ = 10 s, 100 s, 1000 s.
   - Disturbance report: detected periods, odd/even imbalance, outlier rate.
   - Flags: “Periodic-kick”, “Hipp-like”, “Gear-ripple”, “Mainspring drift”, “Sensor offset”.

---

## Presets & Filters (Auto-Selected by Classifier)

| Profile Detected                                  | Windowing (K swings)         | GPS Blend τ   | Filter Core               | Extra Suppression                                 |
|---------------------------------------------------|------------------------------|---------------|---------------------------|---------------------------------------------------|
| Constant-force / weight / good fusee              | 30–120 s MoM                 | 1–3 h         | I-only                    | Optional notch at gear ripple                     |
| Mainspring drift (no fusee)                       | 60–180 s MoM                 | 2–4 h         | I-only + drift term       | Linear drift removal over run                     |
| Per-swing EM asymmetry                            | 20–100 swings MoM + odd/even | 3–6 h         | 2nd-order PLL             | Odd/even pairing; cap per-update phase correction |
| Periodic kick / remontoire (30 s / 60 s)          | N×period (N=2–3) windows     | 2–6 h         | I-only                    | Synchronous averaging + auto-notch                |
| Hipp / random impulses                            | 1–5 min robust MoM           | 3–8 h         | Kalman (phase,freq,amp)   | Amplitude tracking; gating around impulses        |

---

## Drive-Type Disturbance Reference

| Drive / Impulse type                                                        | Dominant disturbance you’ll see                                             | What to measure & windowing                                              | GPS blending / loop filter                                             | Extra tricks that help                                                                 |
|------------------------------------------------------------------------------|------------------------------------------------------------------------------|---------------------------------------------------------------------------|-------------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| Plain mainspring (barrel), no fusee                                          | Slow torque roll-off over hours/days → slow frequency drift; some barrel periodicity (once per turn) | Average period over 30–120 s windows; track frequency + drift (aging)    | I-only loop with very long τ (hours) or 2-state Kalman (freq + drift)   | Very slow feed-forward vs barrel angle if sensed; notch gear periodicity if visible   |
| Mainspring + fusee                                                           | Flattened torque → less secular drift; residual gear ripple                  | Shorter windows OK (10–60 s)                                              | Low-bandwidth PI/PLL or I-only; Kalman optional                         | Notch or median-of-means to suppress known gear periodicities                         |
| Weight drive (gravity)                                                       | Constant torque; gear-train ripple, escapement asymmetry, temp effects       | Windows spanning several gear periods (30–120 s)                          | PI/PLL with very low bandwidth; I-only also works                       | Synchronous averaging or notch filters for gear ripple; temp feed-forward              |
| Electromagnetic impulse every swing (one-sided or timing-biased)             | Cycle-by-cycle phase pulling; possible AM→FM; line-frequency noise           | Ignore single swings; pair odd/even swings; window 20–100 swings          | 2nd-order digital PLL (milli-Hz BW) or Kalman (phase + freq states)     | Keep impulses symmetric; separate amplitude loop; cap per-window correction            |
| Periodic free-swing with 30 s kick (Synchronome-style)                       | Stepwise top-ups → small periodic phase modulation at 30 s                   | Average over multiples of 30 s; median-of-means across 2–3 such blocks    | I-only on frequency, τ in hours; optionally subtract known periodic term | Synchronous detection at 30 s to cancel deterministic phase wiggle                     |
| Hipp toggle / GPO 36 (random-interval impulses when amplitude falls)         | State-dependent, non-uniform impulses; amplitude-frequency coupling          | Long windows (1–5 min) with robust estimators; track amplitude            | Kalman with states [phase, freq, amplitude]; GPS weight very low        | Close loop on amplitude separately to linearize frequency                             |
| Electro clocks that re-tension a spring periodically (E. Howard, Rempe)      | Remontoire-like sawtooth drive every N sec/min → periodic FM                 | Average over integer multiples of tensioning period; store event phase    | PI/PLL with notch at remontoire period (and 2nd harmonic)               | Timestamp tensioning events; exclude short windows around them                         |

---

## Measuring Amplitude and Q

### a) Measuring Amplitude

| Sensor Type                  | Method                                                                                             | Notes                                                                                           |
|------------------------------|----------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------|
| Optical (beam-break / reflective) | Measure time between a defined light threshold on each side of mid-arc. This “half-period arc time” can be mapped to angular displacement. | Works best if you position the beam off-center so the beam is only interrupted over a known arc. |
| Magnetic pickup              | Use induced voltage shape to determine when bob is near coil center and when it leaves.            | Requires good coil alignment; signal can be noisy.                                              |
| Hall effect sensor           | Place sensor offset from midline; detect time bob passes at both offsets.                         | Calibrate distance to angle.                                                                    |
| High-speed photodiode array  | Direct measurement of position vs. time.                                                           | Overkill unless you need very precise motion profiling.                                         |

**Calculation:**
1. For a simple optical gate at mid-arc:
   - Measure the time inside the beam for each swing.
   - Knowing bob speed near the center (from period), convert to distance traveled in the beam.
   - Use geometry to compute maximum angular displacement.
2. Store amplitude history alongside period data.

---

### b) Measuring Q

**Definition:**  
The quality factor (Q) measures how underdamped the pendulum is — the ratio of stored energy to energy lost per cycle.

**Direct decay method (best accuracy):**
1. Let the pendulum swing freely (no drive) until amplitude decays noticeably.
2. Record amplitude per swing: A1, A2, ...
3. Fit an exponential decay:  
   A_n = A_0 × exp(-n × δ)  
   where δ is the logarithmic decrement.
4. Estimate Q ≈ π / δ.

**Logarithmic decrement from two amplitudes:**
- Choose A_n and A_(n+k) separated by k cycles:  
  δ = (1/k) × ln(A_n / A_(n+k))  
  Q ≈ π / δ.  
- Larger k reduces measurement noise.

**Continuous driving method (non-intrusive, in-service):**
- If drive energy per impulse and steady-state amplitude are known:  
  Q ≈ (Energy stored / Energy lost per cycle) × 2π.  
- For small damping, Q is roughly proportional to stored energy divided by average drive energy.

---

### Why Amplitude & Q Matter for Timing

- **Amplitude–Period coupling**:  
  Real pendulums exhibit “circular error” — period increases slightly with amplitude.  
  This means period drift can come purely from amplitude variation.
- **Q–Stability**:
  - High Q → better short-term stability but more sensitivity to slow environmental drift.
  - Low Q → damps quickly after disturbances but more sensitive to drive irregularities.
- **Control loops**:
  - An inner amplitude control loop can keep amplitude constant.
  - Q monitoring can reveal mechanical issues like friction or dirt.

---

## Differentiating "Out of Beat" vs "Off-Centre Sensor"

### Core Idea
- **Out of beat (mechanical):** Asymmetry comes from the escapement. It is largely constant with amplitude and does not flip if you move the sensor.
- **Off-centre sensor (measurement):** Asymmetry comes from where/how you detect “center.” It changes with amplitude in a predictable way and flips sign if you mirror or rotate the sensor.

### Quick Tests (with Expected Outcomes)

| Test                                   | How to Perform                                                                 | If it’s Out of Beat                                     | If it’s Off-Centre Sensor                              |
|----------------------------------------|---------------------------------------------------------------------------------|----------------------------------------------------------|---------------------------------------------------------|
| Amplitude sweep                        | Vary amplitude (or log during natural decay) and plot beat error vs amplitude. | Beat error ~ constant vs amplitude.                      | Beat error scales ~ 1/amplitude.                        |
| Sensor swap / mirror                   | Move sensor to mirrored position or rotate the bracket 180°.                   | Same sign/magnitude (within noise).                      | Sign flips and/or magnitude changes.                    |
| Dual-sensor symmetry                   | Use two sensors symmetrically; compute mid-time = average of the two crossings. | Beat error visible even with mid-time reference.         | Beat error vanishes when using mid-time reference.      |
| Acoustic tick correlation              | Timestamp ticks acoustically; compare tick timing to optical mid-plane.        | Ticks not equidistant about true mid-plane.              | Ticks become equidistant after optical re-centering.    |
| Free-swing baseline                    | Disengage escapement and log half-periods.                                     | Zero beat error free; returns with escapement engaged.   | Non-zero beat error even when free-swinging.            |
| Odd/Even pairing                       | Compare odd vs even half-periods; offset sensor and repeat.                    | Pattern unchanged by small sensor nudges.                | Pattern changes and can be nulled by centering sensor.  |

### Practical Implementation
1. Timestamp both edges of optical gate; derive half-periods, mid-time, and amplitude proxy.
2. Optional: add second optical gate; compute mid-time = (t1 + t2)/2 each swing.
3. Classification:
   - If beat error scales with 1/amplitude → sensor offset.
   - If beat error is constant vs amplitude → out of beat.
   - If flipping/mirroring sensor flips sign → sensor offset.
4. Auto-correction:
   - Estimate offset term and subtract from measured beat error.
   - Report raw and corrected beat error.
5. User guidance:
   - Out of beat → adjust pallet/crutch.
   - Sensor offset → re-center using wizard.

### Rules of Thumb
- Sensor offset → kinematic time bias around true center, shrinks as 1/amplitude.
- Out-of-beat → escapement geometry, constant with amplitude.
- Dual-sensor mid-time → immune to centering errors, reveals true beat error.

## INSTEAD OF PORTl.PINn ISR vectort, try this

## Signal Flow: AC/CCL → EVSYS → TCB Capture → Read TCA0 (Master Clock)

                  ┌───────────────────────────────┐
                  │        External TCXO/OCXO     │   (optional)
                  │ 10 MHz (or other MHz ref)     │
                  └──────────────┬────────────────┘
                                 │
                                 │ (optional via prescaler/divider)
                                 ▼
┌───────────────────────────┐   ┌───────────────────────────┐
│           IR Sensor       │   │         GPS Receiver      │
│ (photodiode/phototrans.)  │   │     PPS (1 Hz or 10 Hz)   │
└──────────────┬────────────┘   └──────────────┬────────────┘
               │                               │
               │ analog level                  │ digital edge
               ▼                               ▼
        ┌──────────────┐                 ┌──────────────┐
        │    AC0       │  (hysteresis)   │   GPIO Pin    │  (e.g., PD0)
        │ Analog Comp. │────────────────▶│    PPS In     │
        └──────┬───────┘                 └──────────────┘
               │  digital, de-noised                   │
               ▼                                        │
        ┌──────────────┐                               │
        │   CCL LUT0   │  (optional invert/gate)       │
        └──────┬───────┘                               │
               │                                        │
               │                                        │
               ▼                                        ▼
        ┌──────────────┐                         ┌──────────────┐
        │  EVSYS CH0   │◀────────────────────────│  EVSYS CH1   │
        │  (from CCL)  │                         │ (from PD0)   │
        └──────┬───────┘                         └──────┬───────┘
               │                                        │
               ▼                                        ▼
        ┌──────────────┐                         ┌──────────────┐
        │   TCB1       │  CAPT event (both edges)│   TCB2       │  CAPT event (rising)
        │  IR Capture  │────────────────────────▶│  PPS Capture │
        └──────┬───────┘                         └──────┬───────┘
               │ CAPT ISR reads                        │ CAPT ISR reads
               ▼                                        ▼
        ┌──────────────────────────┐           ┌──────────────────────────┐
        │        TCA0 (master)     │           │        TCA0 (master)     │
        │  16-bit free-running CNT │◀──────────│  same counter, same time │
        │  (CLK_PER or ext. ref)   │           │  base for all timestamps │
        └───────────┬──────────────┘           └───────────┬──────────────┘
                    │                                      │
                    └──────────────► Software: discipline ticks/second to PPS
                                      (EMA/PLL/Kalman, long time constant)


# GPS PPS Averaging Guidelines – ATmega4809 + Adafruit Ultimate GPS

## Summary
For the Adafruit Ultimate GPS (commodity PPS) and an ATmega4809 timer clock:

- **Short-term smoothing (display/phase readout):** average **64–128 seconds** of PPS phase error.  
- **Long-term disciplining (frequency trim):** average **15–30 minutes** (900–1800 seconds).  
- **Outlier rejection:** drop any PPS sample that’s off by **> 300 ns** or **> 3× RMS jitter**.  
- **Resolution limit:** with a 20 MHz timer (50 ns tick), don’t expect meaningful stability below ~10–20 ns after averaging.

---

## Why These Values
- Second-to-second PPS jitter on low-cost GPS: **tens to ~100 ns**.  
- Averaging `N` seconds reduces white jitter by about `1/sqrt(N)`.  
  - 64 s → ~8× improvement  
  - 128 s → ~11× improvement  
- Averaging too long starts to follow GPS *wander* (ionospheric delay, satellite geometry).  
  - Diminishing returns after about **15–30 minutes**.

---

## Measurement Setup

1. **Capture PPS edges** with a free-running timer.  
   - Example: 20 MHz clock → 50 ns ticks.  
   - Let `N0 = ticks_per_second` (e.g., 20,000,000).  
   - On each PPS pulse `k`, capture timer count `Ck`.

2. **Compute phase error (seconds):**
   phase_ticks = (Ck - k * N0) wrapped to [-N0/2, N0/2)
   phase_seconds = phase_ticks / tick_rate

3. Compute period error (ticks per second):
	period_ticks = (Ck - Ck-1) - N0

## Filtering Strategy
- Short-term smoothing (for display/phase readout)
	- Use a moving average of last 64–128 seconds OR
	- Exponential moving average (EMA) with time constant tau_s ≈ 80–120 seconds
		- Gain alpha_s ≈ 1/tau_s per second (e.g., alpha_s ≈ 0.01 for 100 s)
- Long-term disciplining (for frequency trim)
	- Smooth period_ticks with EMA time constant tau_f = 900–1800 seconds (15–30 min)
		- Gain alpha_f ≈ 1/tau_f (e.g., 0.0011 for 900 s, 0.00056 for 1800 s)
	- Use the result to adjust clock frequency (prescaler, DFLL step, or software correction).

## Outlier Rejection
Before feeding into either filter:
- Reject any PPS sample where abs(phase_seconds) > 300 ns
- Or reject if > 3× the current RMS jitter.

## Resolution & Limits
- At 20 MHz, timer resolution is 50 ns.
- Averaging over many seconds can get below that (due to dithering from PPS jitter).
- Don’t expect meaningful stability below ~10–20 ns on the display.

## Recommended Starting Values
| Purpose                   | Method | Time Constant | Alpha   | Notes                  |
| ------------------------- | ------ | ------------- | ------- | ---------------------- |
| Phase smoothing (display) | EMA    | 100 s         | 0.01    | Keeps display steady   |
| Frequency disciplining    | EMA    | 1800 s        | 0.00056 | Tracks long-term drift |
| Outlier rejection         | N/A    | N/A           | N/A     | Drop >300 ns or >3σ    |

