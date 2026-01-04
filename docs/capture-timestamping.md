# Capture timing on RP2040 (arduino-pico): options and recommendation

## Context
Legacy Nano Every used **EVSYS → TCB capture** to get precise hardware timestamps for:
- optical sensor rising/falling edges (pendulum)
- GPS PPS edges

RP2040 has no direct equivalent, so we choose between:
- CPU interrupt timestamping (simple, but includes ISR latency/jitter), and
- PIO/DMA-based capture (deterministic, EVSYS-like).

---

## Option A — GPIO IRQ on Core1 + `rp2040.getCycleCount64()`
### What it is
- Configure GPIO edge interrupts.
- In ISR, immediately record `t_cycles = rp2040.getCycleCount64()` and enqueue an event.

### Benefits
- Minimal code and easy debugging.
- Works directly in “cycles” (one unit).
- Often more than enough for PPS.

### Tradeoffs
- The recorded time is when the ISR begins executing (includes latency).
- Latency varies with interrupt masking and any higher priority work.
- Under combined WiFi + SD + HTTP load, worst-case jitter may increase.

### Best use
- PPS (1 Hz) in first iteration.
- As a cross-check/reference channel for validating PIO capture.

---

## Option B — PIO edge capture → FIFO (recommended for pendulum edges)
### What it is
- A PIO state machine monitors the input pin and detects edges.
- On each edge, PIO pushes a timestamp token into its RX FIFO.
- Core1 drains FIFO and reconstructs the swing.

### Benefits
- Deterministic timing largely independent of CPU interrupt latency.
- “EVSYS-like” capture quality for high-value edge events.
- CPU work is reduced to “drain FIFO and process”.

### Tradeoffs
- Requires a PIO program and initialization.
- FIFO is shallow (bounded buffering); must drain promptly.
- Must define timestamp representation:
  - PIO-local counter + wrap extension
  - or encoding edge intervals, depending on implementation choice

### Best use
- Pendulum sensor rising/falling edges (primary accuracy path).

---

## Option C — PIO capture + DMA → RAM ring (maximum robustness)
### What it is
- PIO pushes timestamp tokens.
- DMA transfers them into a RAM circular buffer automatically.

### Benefits
- Very low CPU overhead.
- Best resistance to bursty edges and “system under load” scenarios.
- Simplifies Core1 real-time behavior: processing a RAM ring can be deterministic.

### Tradeoffs
- Highest implementation complexity.
- Requires careful buffer sizing and explicit overflow accounting.

### Best use
- If FIFO draining proves fragile or you want continuous raw event logging.

---

## Recommended implementation sequence
1. **Bring-up**: PPS on GPIO IRQ + cycle counter; pendulum edges on PIO→FIFO.
2. **Instrument**: queue high-water marks, FIFO drain latency, dropped-event counters, ISR occupancy.
3. **Upgrade if needed**: if FIFO overflow/drops occur under WiFi+SD load, migrate pendulum capture to PIO+DMA.
4. **Only if needed**: move PPS to PIO as well, if measurable jitter correlates with system load.

---

## Acceptance criteria for capture choice
- At expected pendulum rates: `dropped_events == 0` over multi-hour runs.
- PPS lock is stable; `corr_blend_ppm_x1e6` is smooth vs `corr_inst_ppm_x1e6`.
- Under WiFi/HTTP polling + SD logging:
  - no systematic increase in edge timing variance beyond acceptable threshold
  - capture core remains responsive; no stalls
