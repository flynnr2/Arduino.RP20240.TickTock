# Core0 OLED UI

OLED rendering is a presentation layer over `LatestState`. It must not block ingest and must not do I/O other than display updates.

---

## Display assumptions
- OLED on I2C (shared bus with sensors).
- Render cadence: configurable (default **2–10 Hz**).

---

## Data sources
OLED reads only from `LatestState`, which includes:
- last `SwingRecordV1`
- derived conversions using the **stable-first** PPS scale (`pps_cycles_last_good`)
- rolling aggregates (mean/median + variability proxies)
- health counters (SD/WiFi/sensors)

---

## Page model
Implement a small set of pages that can rotate automatically and/or via a button:

### Page 1 — Timing (latest + summary)
- `gps_state`
- latest period (cycles and seconds if scale valid)
- PPS age (`pps_age_ms` or `pps_age_cycles`)
- rolling mean period (seconds) + variability proxy (prefer median/MAD)
- key flags (overflow/dropped/glitch indicators)

### Page 2 — Environment
- temperature (°C)
- humidity (%RH)
- pressure (hPa)
- sensor availability indicators

### Page 3 — Health
- SD: mounted?, current raw/stats file, last error (short)
- WiFi: mode (STA/AP), RSSI
- counts: ring overflow, dropped, glitch, PPS outliers
- config version/reset count (optional)

Optional:
- page for IP address / AP SSID

---

## Update cadence and performance
- Rendering should be incremental and bounded.
- Avoid per-frame dynamic allocation.
- Keep string formatting minimal; precompute formatted strings in `LatestState` if needed.

---

## Error surfacing
- If SD unavailable: show “SD: ERR” and an error code; set `FLAG_SD_ERROR`.
- If WiFi down: show “WiFi: DOWN” and mode; set `FLAG_WIFI_DOWN`.
- If sensors missing: show “SHT41: —” etc.; set `FLAG_SENSOR_MISSING`.

---

## Acceptance checks
- OLED updates smoothly under WiFi and SD load.
- OLED code does not perform sensor reads or SD operations.
- A missing OLED does not break the system (UI can be disabled).


---

## Variability display recommendation
For pendulums, occasional glitches are common; prefer robust metrics:
- show **median period** and **MAD** (or MAD-derived ppm) as the primary variability indicator
- mean/std can be secondary
