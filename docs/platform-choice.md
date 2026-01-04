# Platform choice: Nano RP2040 Connect dual-core build

*(See other docs in this pack for full project context.)*

## Goals that drive the choice
- Dual-core split: Core1 capture, Core0 app
- Core1 uses one timing unit end-to-end (prefer cycles)

## Option A: arduino-pico core
- Recommended default for straightforward dual-core.

## Option B: Arduino Mbed core
- Fallback if peripheral/library compatibility requires it.

## Timestamp backend decision (Core1 “one unit”)

## Accurate timestamping options on RP2040 (with arduino-pico)
RP2040 does not have an AVR-style EVSYS→timer-capture path, so “hardware timestamps” are achieved via **PIO** (and optionally DMA) or approximated via **GPIO IRQ latency**.

### 1) GPIO interrupt (Core1) + cycle counter (simplest)
- Attach GPIO edge interrupts.
- In ISR, record `t_cycles = rp2040.getCycleCount64()` (single unit: CPU cycles).

**Benefits**
- Fastest to implement and debug.
- Uses *cycles* directly (matches “one unit only” requirement).
- Often sufficient for **PPS** (1 Hz) and can be adequate for pendulum edges if system load/jitter is controlled.

**Tradeoffs**
- Timestamp reflects **when the ISR runs**, not when the edge occurred:
  - interrupt latency/jitter from masking, flash stalls, other IRQs
  - worst-case jitter may correlate with heavy Core0 activity (WiFi/SD), even if Core1 is isolated.

### 2) PIO edge-capture → FIFO (EVSYS-like, recommended for pendulum)
- A PIO state machine watches the input pin, detects edges, and pushes a timestamp/token into its RX FIFO.
- Core1 drains the FIFO and reconstructs swings.

**Benefits**
- Much closer to EVSYS capture: edge is latched by deterministic PIO timing, not ISR latency.
- Very low jitter and stable timing under system load.

**Tradeoffs**
- More engineering (PIO program + setup + timestamp extension/wrap handling).
- Limited FIFO depth; must drain promptly and track overflow.

### 3) PIO edge-capture + DMA → RAM ring (highest robustness)
- PIO produces timestamp words; DMA streams them into a circular buffer in RAM.

**Benefits**
- Near-zero CPU overhead; best for bursty edges and “no loss under load”.
- Clean separation: Core1 processes a RAM ring; Core0 stays out of the way.

**Tradeoffs**
- Highest complexity (PIO + DMA + ring management).
- Requires careful overflow/diagnostics (high-water marks, drop counters).

### Recommended combination (first implementation)
- **Pendulum edges:** PIO capture (start with FIFO, upgrade to DMA if needed)
- **GPS PPS:** GPIO IRQ + cycle counter initially; move to PIO only if jitter under load proves problematic

This gives EVSYS-like determinism where it matters most, while keeping the first bring-up manageable.


- Prefer cycles; allow monotonic ticks for first bring-up behind an abstraction.
