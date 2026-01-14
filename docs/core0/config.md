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
- expose “config_reset_count” in `/status`

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
- `logs_dir` (optional constant)

### Sensors/UI
- `sensor_poll_ms`
- `oled_enabled`
- `oled_refresh_ms`
- `page_rotation_ms`

### Diagnostics
- `status_publish_ms`
- optional debug flags

---

## Update mechanism
- via HTTP config portal (`/wifi` and/or `/config`) or hardcoded defaults during bring-up.
- After changing credentials: reconnect or reboot.

---

## Acceptance checks
- Corrupt config falls back to defaults without bricking.
- WiFi creds persist across reboot.
- Version bump is handled (migrate or reset with clear status).
