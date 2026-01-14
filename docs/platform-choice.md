# Platform choice: Nano RP2040 Connect dual-core build

## Decision (locked)
- **Arduino core:** **arduino-pico** (Earle Philhower RP2040 core)
- **Capture strategy:** **Unified PIO edge capture + DMA → RAM ring** for **both** pendulum sensor edges **and** GPS PPS edges
- **Core split:** Core1 = capture + PPS discipline; Core0 = WiFi/HTTP/SD/sensors/display/stats

## Accurate timestamping options on RP2040 (arduino-pico)
- **PIO + DMA → RAM ring (chosen):** deterministic edge capture, robust under WiFi/SD load, best for fast pendulums; use the same pipeline for PPS.
- **PIO → FIFO:** simpler, but drops occur if CPU draining falls behind.
- **GPIO IRQ + cycle counter:** simplest, but includes ISR latency/jitter.

## Recommended combination
- **Pendulum edges:** PIO + DMA → RAM ring (**chosen**)
- **GPS PPS:** PIO + DMA → RAM ring (**chosen**) — same event pipeline and timebase as pendulum edges
- **Reason:** eliminates ISR-latency coupling and makes “simultaneous” events deterministic (ties are valid)

## Collision/tie handling and resolution
- With unified PIO timestamps, PPS and pendulum edges can legitimately share the same timestamp when they occur within one tick.
- Treat equal timestamps as valid; downstream logic must not assume strict ordering.
- Choose a tick resolution that makes quantization negligible vs your error budget (often 1 µs or better is plenty; faster ticks provide extra margin).

## Logging schema (chosen)
- Use a **single combined record (one row per swing)**, raw-first, units in **cycles**.
- Include PPS freshness (`pps_age_cycles`) and `pps_new`+`pps_interval_cycles_raw` to reconstruct PPS stream.
See `docs/core1/logging-schema.md`.
