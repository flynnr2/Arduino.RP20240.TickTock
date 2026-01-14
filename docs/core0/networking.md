# Core0 Networking (WiFi + HTTP)

Core0 provides connectivity, configuration, and live telemetry endpoints. HTTP must be responsive and non-blocking.

---

## WiFi behavior

### Modes
- **STA preferred**: connect using stored credentials.
- **AP fallback (optional)**: if STA fails for a configured window, start AP for provisioning.

### Reconnect policy
- Exponential backoff on reconnect attempts.
- Never block ingest while reconnecting.

### Provisioning (if enabled)
- Minimal config portal:
  - view current status
  - submit SSID/password
  - persist config and reboot (or reconnect)

---

## HTTP endpoints (recommended baseline)

### `GET /latest` (JSON)
Returns:
- last `SwingRecordV1` fields
- derived values (period, pps_age_ms, etc.)
- latest env sample (temp/rh/press) if available

Constraints:
- Must not touch SD (except optional file metadata cached elsewhere).
- Must be fast: serialize from `LatestState`.

### `GET /status` (JSON)
Returns:
- health counters: drops/overflows/glitches, counts by `gps_state`
- SD state: mounted?, last error, bytes written, current file name
- WiFi state: STA/AP, RSSI, reconnect count
- uptime, heap/free memory

### `GET /stats` (JSON) (optional)
Returns rolling aggregates if enabled:
- rolling mean/median period, variability proxy, etc.

### `GET /files`
- list log files (HTML or JSON)
- should not scan SD repeatedly; cache results or refresh on a slow cadence

### `GET /download?name=...`
- stream an SD file
- allowed to perform SD reads, but do not block ingest:
  - chunked read/write
  - yield between chunks

### `GET/POST /wifi` (optional)
- config portal endpoints (STA/AP setup)

---

## Non-blocking rules
- No I2C reads in handlers.
- No long computations in handlers.
- SD reads only inside `/download`, chunked.

---

## Acceptance checks
- With SD logging active and OLED refreshing, `/latest` remains responsive.
- WiFi reconnect does not starve ingest (queue drain continues).
- `/download` streams files without crashing; if SD missing, returns a clear error.
