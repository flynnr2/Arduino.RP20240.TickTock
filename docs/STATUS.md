# Status / Health Reporting

Core0 should expose a clear view of runtime health via:
- HTTP `GET /status` (JSON)
- OLED “Health” page
- Serial `status` command (CLI)

---

## Recommended `/status` fields
- `uptime_ms`
- `gps_state` (latest)
- `pps_age_cycles` and derived `pps_age_ms`
- `scale_valid` and `pps_cycles_last_good`
- `flags_last` (from last swing)
- `flags_counts` (counters by bit)
- `gps_state_counts` (counters)
- SD:
  - `sd_available`, `raw_file`, `stats_file`, `bytes_written`, `last_sd_error`
- WiFi:
  - `mode` (STA/AP), `connected`, `rssi`, `reconnect_count`, `ip`
- Sensors:
  - availability flags, last env sample, `sensor_error_count`
- Config:
  - version, `config_reset_count`, key tunables summary

---

## Flags and counters
Use the `flags` bit assignments in `docs/shared/interfaces.md`. Core0 should set:
- `FLAG_TIME_INVALID` when time-of-day is not valid (affects daily rotation)
- `FLAG_SD_ERROR`, `FLAG_WIFI_DOWN`, `FLAG_SENSOR_MISSING`, `FLAG_OLED_ERROR` as applicable

---

## Degradation transparency
If any subsystem is degraded, `/status` and OLED should reflect it immediately.

---

## Operational constraints (for future reference)
### Firmware identity (recommended)
Expose a stable identity in `/status` and log headers:
- `fw_version` (semantic version or build tag)
- `build_time_utc` (or build timestamp)
- `git_hash` (short hash if available)

### Loss / overflow budget
In addition to last flags, track cumulative and rate-based indicators:
- `lost_swings_total` (if tracked)
- `ring_overflow_events`
- `dropped_events_total`
- optional: per-minute rate (computed over a small rolling window)

### Environmental sampling timing
Environmental values in logs and `/latest` are the **most recent cached sample** at the time of emission/log write.
(They are typically polled at ~1 Hz; they are not phase-aligned to swing edges.)

### Operational envelope (planning)
Document expected maximum swing/edge rates (e.g., fast pendulum scenarios) so buffer sizing and SD policies can be validated against a target.
