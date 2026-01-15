# Core0 Configuration

Core0 stores persistent configuration such as WiFi credentials and runtime tunables. Configuration must be versioned and CRC-protected.

---

## Storage
- Use a versioned struct with:
  - `magic`
  - `version`
  - payload fields
  - `crc16` (or CRC32)

On CRC failure:
- load defaults
- increment `config_reset_count`
- surface this in `/status`

Implementation options on RP2040:
- EEPROM emulation (arduino-pico) **or**
- LittleFS with an atomic write pattern (write-new + rename).

---

## Keys (initial set)

### Networking
- `wifi_ssid`
- `wifi_pass`
- `wifi_mode_preference` (STA preferred; AP fallback on/off)
- `ap_ssid` / `ap_pass` (optional)

### Logging
- `logging_enabled`
- `rotation_mode` (daily vs size)
- `flush_lines`
- `flush_ms`
- `max_file_size_bytes` (if size rotation)
- `time_valid_max_age_s` (NTP time staleness for daily rotation)
- `logs_dir` (optional constant)

### Stats / processing
- `stats_enabled`
- `stats_emit_ms` (cadence for stats CSV snapshots; e.g., 10s)
- rolling window sizes (by swings and/or seconds)
- PPS quality gates (outlier thresholding) used by Core0 for stable-first scale updates

### Sensors/UI
- `sensor_poll_ms`
- `oled_enabled`
- `oled_refresh_ms`
- `page_rotation_ms`

### Diagnostics
- `status_publish_ms`
- optional debug flags

---

## Tunables interface

### Serial CLI (recommended for bring-up)
Provide a minimal command interface on Core0:
- `status` — print health + config summary + last gps_state + last pps_age
- `get <key>`
- `set <key> <value>`
- `save` / `load` / `defaults`
- `help`

This is the fastest way to iterate without committing to a full web UI.

### HTTP config (optional)
- `/config` GET/POST mirrors the same keys as the Serial CLI.
- After changing credentials: reconnect or reboot.

---

## Acceptance checks
- Corrupt config falls back to defaults without bricking.
- WiFi creds persist across reboot.
- Version bump is handled (migrate or reset with clear status).
- Serial `get/set/save/load/defaults` works and is reflected in `/status`.
