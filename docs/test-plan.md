# Test plan: RP2040 dual-core TickTock system

## 0) Philosophy
We test in layers:
1. **Platform stability** (WiFi+SD+dual-core doesn’t hang)
2. **Capture correctness** (edges → swing reconstruction)
3. **Discipline correctness** (PPS → correction outputs)
4. **System under load** (WiFi + HTTP + SD + sensors simultaneously)

---

## 1) Phase-0 smoke tests (before full port)

### T0.1 Dual-core sanity
- Core1: toggles a GPIO at 10 Hz and increments a counter
- Core0: prints the Core1 counter once per second

Pass:
- counts advance monotonically
- no stalls for 30 minutes

### T0.2 WiFi + HTTP baseline
- Core0: connect STA (or start AP) and serve `/status`
- poll `/status` every 1s for 30 minutes

Pass:
- no hangs, no watchdog resets
- no progressive latency growth

### T0.3 SD baseline
- Core0: append a small line every second; flush every N lines
- run for 60 minutes

Pass:
- file is readable, lines intact, no corruption
- no stalls

### T0.4 Combined baseline
- WiFi + SD + dual-core toggling

Pass:
- stable for 60 minutes

---

## 2) Capture correctness tests (Core1)

### T1.1 Edge ISR rate + queue robustness
- Simulate edge input (function generator or loopback) at:
  - low rate (1 Hz)
  - expected pendulum edge rate
  - stress rate (10× expected)

Pass:
- `dropped_events == 0` at expected rate
- overflow behavior is well-defined and visible at stress rate

### T1.2 Swing reconstruction invariants
For each completed swing:
- `tick_ticks > 0`, `tock_ticks > 0`
- `tick_block_ticks > 0`, `tock_block_ticks > 0`
- `tick_ticks + tock_ticks` close to expected period (within sanity range)
- block times plausible and stable

Pass:
- invariants hold for 10k+ swings

---

## 3) PPS discipline correctness tests (Core1 + Core0 reporting)

### T2.1 PPS lock acquisition
- With good GPS:
  - start from NO_PPS → ACQUIRING → LOCKED

Pass:
- reaches LOCKED within expected time window
- does not flap between states under stable conditions

### T2.2 Jitter/outlier resilience
- Introduce artificial PPS jitter or dropouts (if possible), or test with known-bad reception

Pass:
- Hampel/median stages prevent wild correction spikes
- state machine transitions as intended (HOLDOVER/BAD_JITTER handling)
- corrections remain bounded

### T2.3 Correction magnitude sanity
Record:
- `corr_inst_ppm_x1e6` and `corr_blend_ppm_x1e6`

Pass:
- values fall in plausible range for oscillator drift (typically tens–hundreds ppm equivalent, not millions)
- blended correction is smoother than instantaneous under jitter

---

## 4) System tests under realistic load (Core0 heavy)

### T3.1 SD + WiFi + HTTP polling + sensors
- Poll `/stats.json` at 1 Hz
- SD logging active
- sensors active
- OLED updates active

Pass:
- no missing samples at expected pendulum rate
- no queue overflow
- no WiFi lockups

### T3.2 AP provisioning + live capture
- Force STA credential failure → AP mode
- Connect client, change creds, reconnect STA

Pass:
- capture continues without drops
- no reboot loops
- config changes propagate to Core1 (tunables `version` increments)

---

## 5) Regression comparisons vs legacy system
Enable a **legacy-compat debug mode** on Core0 that prints the old CSV line schema.

Compare:
- distribution of `tick/tock` and `block` times
- `gps_status` state transitions timing
- correction trends over hours

Pass:
- no systematic bias introduced by new timestamp backend
- variance not worse than legacy baseline (or explainable)

---

## 6) Diagnostics to expose (must-have)
- Queue fill level high-water mark
- `dropped_events` (per-sample and cumulative)
- PPS quality: R/J metrics and lock state
- uptime, heap/stack watermark (Core0), loop service latency histogram (optional)
