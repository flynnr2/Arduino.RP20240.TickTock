# Core0 Networking (WiFi + HTTP)

Core0 provides connectivity, configuration, and live telemetry endpoints. HTTP must be responsive and non-blocking.

---

## WiFi behavior

### Modes
- **STA preferred**: connect using stored credentials.
- **AP fallback (optional)**: if STA fails for a configured window, start AP for provisioning/config.

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

All handlers should read from `LatestState` and return quickly.

### `GET /latest` (JSON)
Returns:
- last `SwingRecordV1` fields
- derived convenience values (e.g., `period_s`, `pps_age_ms`, and scale validity)
- latest env sample (temp/rh/press) if available

Constraints:
- Must not touch SD.
- Must be fast: serialize from `LatestState`.

### `GET /status` (JSON)
Returns:
- health counters: drops/overflows/glitches, counts by `gps_state`, flags summaries
- SD state: mounted?, last error, bytes written, current file names (raw/stats)
- WiFi state: STA/AP, RSSI, reconnect count
- uptime, heap/free memory, config version/hash

### `GET /stats` (JSON)
Returns the current rolling statistics snapshot as `StatsRecordV1` (computed primarily from `usable_for_stats` samples per `docs/core0/processing.md`) (see `docs/shared/interfaces.md`), plus any formatted fields useful for UI.

Notes:
- `StatsRecordV1` is derived and should use the **stable-first** PPS scale policy (do not chase in `ACQUIRING`).

### `GET /files`
- list log files (HTML or JSON)
- should not rescan SD repeatedly; cache results or refresh on a slow cadence

### `GET /download?name=...`
- stream an SD file (raw or stats)
- allowed to perform SD reads, but must not starve ingest:
  - chunked read/write
  - yield between chunks

### `GET/POST /config` (optional)
- view and update config/tunables
- for bring-up, a Serial CLI is usually the fastest interface; `/config` can mirror it later.

### `GET/POST /wifi` (optional)
- credential management endpoints (if not folded into `/config`)

---

## Non-blocking rules
- No I2C reads in handlers.
- No long computations in handlers.
- SD reads only inside `/download`, chunked.

---

## Acceptance checks
- With SD logging active and OLED refreshing, `/latest` and `/status` remain responsive.
- WiFi reconnect does not starve ingest (queue drain continues).
- `/download` streams files without crashing; if SD missing, returns a clear error.
