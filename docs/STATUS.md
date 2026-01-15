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
