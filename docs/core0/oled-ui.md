# Core0 OLED UI

OLED rendering is a presentation layer over `LatestState`. It must not block ingest and must not do I/O other than display updates.

---

## Display assumptions
- OLED on I2C (shared bus with sensors).
- Render cadence: configurable (default **2–10 Hz**).

---

## Page model
Implement a small set of pages that can rotate automatically and/or via a button:

### Page 1 — Timing
- `gps_state`
- latest period (derived from cycles)
- PPS age (`pps_age_ms` or equivalent)
- key flags (overflow/dropped/glitch indicators)

### Page 2 — Environment
- temperature (°C)
- humidity (%RH)
- pressure (hPa)
- sensor availability indicators

### Page 3 — Health
- SD: mounted?, current file, last error (short)
- WiFi: mode (STA/AP), RSSI
- counts: ring overflow, dropped, glitch, PPS outliers

Optional:
- page for IP address / AP SSID

---

## Update cadence and performance
- Rendering should be incremental and bounded.
- Avoid per-frame dynamic allocation.
- Keep string formatting minimal; precompute formatted strings in `LatestState` if needed.

---

## Error surfacing
- If SD unavailable: show “SD: ERR” and an error code.
- If WiFi down: show “WiFi: DOWN” and mode.
- If sensors missing: show “SHT41: —” etc.

---

## Acceptance checks
- OLED updates smoothly under WiFi and SD load.
- OLED code does not perform sensor reads or SD operations.
- A missing OLED does not break the system (UI can be disabled).
