# Platform choice: Nano RP2040 Connect dual-core build

## Decision (locked)
- **Arduino core:** **arduino-pico** (Earle Philhower RP2040 core)
- **Capture strategy:** **PIO edge capture + DMA → RAM ring buffer** from day one
- **Core split:** Core1 = capture + PPS discipline; Core0 = WiFi/HTTP/SD/sensors/display/stats


## Accurate timestamping options on RP2040 (arduino-pico)
- **PIO + DMA → RAM ring (chosen):** deterministic edge capture, robust under WiFi/SD load, best for fast pendulums.
- **PIO → FIFO (not chosen):** simpler, but drops occur if CPU draining falls behind.
- **GPIO IRQ + cycle counter:** simplest; good for PPS; has ISR latency/jitter.

## Recommended combination
- **Pendulum edges:** PIO + DMA → RAM ring (**chosen**)
- **GPS PPS:** start with GPIO IRQ + `rp2040.getCycleCount64()` or use PIO+DMA for uniformity
