# Core0 Sensors

Core0 polls environmental sensors and caches the latest readings for logging, UI, and HTTP. Sensor I/O must never block Core1 ingest.

---

## Scope
- Supported (initial): **SHT41** (temperature/humidity), **BMP280** (pressure/temperature).
- Transport: **I2C** (shared bus with OLED if applicable).
- Output: update `LatestState.env` (cached) and expose via `/latest` and `/status`.

---

## Data model

### `LatestState.env` (recommended fields)
- `env_valid` (bool)
- `temp_c` (float)
- `rh_pct` (float)
- `press_hpa` (float)
- `env_ts_ms` (u32, `millis()` timestamp)

### Sensor availability flags
Expose in `/status` and OLED “health” page:
- `has_sht41` (bool)
- `has_bmp280` (bool)
- `sensor_error_count` (u32)
- `last_sensor_error` (string or enum code)

---

## Poll cadence
- Default: **1 Hz** (configurable).
- Policy:
  - Poll on a scheduler tick (not inside HTTP handlers).
  - If a poll overruns (bus hang), abort quickly (timeout) and increment error counters.
  - Never retry aggressively in a tight loop—use backoff (e.g., wait until next scheduled poll).

---

## Initialization and detection
On boot:
1. Init I2C (set clock speed; keep consistent with OLED requirements).
2. Probe devices:
   - SHT41: device presence via begin/probe.
   - BMP280: probe at expected address(es).
3. Record availability flags and surface them in `/status`.

---

## Error handling
- Missing device: set `has_* = false`, keep running.
- Read failure/CRC failure: keep last-good sample but set `env_valid=false` if stale beyond horizon (e.g., >10s).
- Bus contention/hang: enforce an I2C timeout strategy where possible; otherwise treat as a fatal sensor read failure and continue.

---

## Logging
If desired, append environmental fields to the swing CSV row (Core0 choice):
- `temp_c,rh_pct,press_hpa`
- If a sensor is missing, log empty field or a sentinel (prefer empty for CSV compatibility).

---

## Acceptance checks
- Runs with:
  - both sensors present
  - only one present
  - none present
  without crashing or blocking ingest.
- `/latest` includes current env sample (or clearly indicates invalid/missing).
- Sensor poll cadence remains stable under WiFi/HTTP load.
