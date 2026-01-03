# Platform choice: Nano RP2040 Connect dual-core build

## Goals that drive the choice
- True **dual-core split**:
  - **Core 1**: time-critical capture + PPS discipline (no WiFi/SD/HTTP)
  - **Core 0**: WiFi/AP, HTTP, SD logging, sensors, display, stats, conversions
- Capture core uses **one internal timing unit** end-to-end (preferably **CPU cycles**).
- App core may do all conversions, formatting, logging schema, and UI.

## Option A: Earle Philhower `arduino-pico` core (recommended default)
Why it fits:
- Simple dual-core model: `setup1()` / `loop1()` for Core 1.
- Provides convenient low-level access for high-resolution timing / cycle counting.
- Generally easier to implement “hard separation” between cores.

Risks / things to validate early:
- Nano RP2040 Connect uses **u-blox NINA-W102 (ESP32) WiFi module** via SPI.  
  Ensure the WiFi stack you need (WiFiNINA + HTTP server) works reliably with this core.
- Library compatibility: some Arduino libraries assume the official Mbed core.

**Phase-0 acceptance tests (must pass before porting everything):**
1. Dual-core runs: Core1 toggles a pin at a known rate while Core0 services loop normally.
2. WiFi:
   - STA connect + serve `/status` page for 30+ minutes
   - AP mode start + connect from phone/laptop + serve a config page
   - Reconnect behavior when AP/STA credentials change
3. SD:
   - Open/append/flush loop while WiFi is active
   - Confirm no deadlocks/hangs over 30+ minutes
4. ISR timing:
   - Capture ISR runs while WiFi/SD are active; ensure queue doesn’t overflow at expected edge rates

If any of these fail, document:
- exact board core version
- exact library versions
- minimal repro sketch
- symptom (hang, crash, corrupt SD, WiFi dropouts)

## Option B: Official Arduino Mbed OS core (fallback)
Pros:
- “Official” support; tends to be safest for Nano RP2040 Connect peripherals.
- WiFiNINA compatibility is often better.

Cons:
- Dual-core is **not** as straightforward; you may need to manually launch Core1 and carefully manage shared state.
- Harder to keep capture logic completely insulated from Mbed/WiFi activity.

Use this if:
- WiFiNINA + HTTP is unstable on arduino-pico and you don’t want to patch.

## Timestamp backend decision (Core1 “one unit”)
### Preferred: CPU cycles (single unit)
- Store edge timestamps as `uint64_t t_cycles`.
- Convert to seconds/us only on Core0 for display/logging.

### Acceptable first bring-up: monotonic ticks (still “one unit”)
- Store timestamps as `uint64_t t_ticks` in a monotonic microsecond timer.
- Keep the API as `now_ticks()` so the backend can switch to cycles later without changing swing/PPS logic.

**Recommendation:** start with whatever passes Phase-0 quickest, but keep the **timestamp source abstract** from day one.

## Non-goals (explicit)
- Do not preserve the old serial “tagged CSV” protocol internally.
- Do not do unit switching (16MHz/us/ns/ms) on Core1.
