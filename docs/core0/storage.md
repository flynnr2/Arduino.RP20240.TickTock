# Core0 Storage (SD Logging)

Core0 owns SD card initialization, file management, and logging. Logging must degrade gracefully: SD failure must not affect capture or ingest.

---

## Goals
- Append-only CSV logging of **one row per swing** (`SwingRecordV1`).
- Versioned header and schema compatibility for post-processing.
- Robustness to SD absence/removal and slow writes.

---

## File layout and naming
Recommended:
- Directory: `/logs/`
- File name patterns:
  - Daily: `logs/YYYY-MM-DD.csv`
  - Size-rotated: `logs/runNNN_partMMM.csv` (or timestamp-based)

Choose one rotation policy at a time (configurable).

---

## CSV format

### Header (recommended)
Line 1 (metadata):
- `schema_version=1,units=cycles`

Line 2 (columns):
- `swing_id,pps_id,pps_age_cycles,pps_new,pps_interval_cycles_raw,tick_block_cycles,tick_cycles,tock_block_cycles,tock_cycles,gps_state,flags`

Optional appended fields (if enabled):
- `temp_degC,rh_pct,press_hPa`

### Row semantics
- When `pps_new==0`, log `pps_interval_cycles_raw=0` (preferred).
- `gps_state` is authoritative 5-state enum.
- `flags` is a bitfield; maintain backward compatibility or bump schema_version.

---

## Write and flush policy
- Append one row per ingested swing record.
- Buffer writes and flush:
  - every **N lines** (e.g., 50–200), or
  - every **M seconds** (e.g., 2–5s),
  whichever occurs first.
- Avoid per-line `flush()` calls.

### Blocking limits
- SD writes can be slow; do not block ingest for long.
- If logging falls behind:
  - keep ingesting and updating `LatestState`,
  - set a “logging lag” indicator/counter in `/status`,
  - resume logging when SD recovers.

---

## Rotation policy
Pick one (config-controlled):
1. **Daily rotation**: roll at local date boundary once time is valid.
2. **Size rotation**: roll when file exceeds threshold (e.g., 25–200 MB).

Time source for “daily”:
- Prefer network time when available; if not, keep “unknown date” filenames until time becomes valid.

---

## Failure handling
- SD missing at boot:
  - set `sd_available=false`,
  - keep running; `/status` reports SD unavailable.
- SD removed mid-run:
  - detect write/open failure, close file, set error counters,
  - continue running with logging disabled until reinserted (optional re-mount attempt).
- Corruption:
  - never overwrite existing logs; open new file if needed.

---

## HTTP integration
Storage supports:
- list log files for `/files`
- streaming download for `/download?name=...` (read-only)

---

## Acceptance checks
- Produces a CSV compatible with post-processing scripts.
- Daily or size rotation works as configured.
- Removing SD mid-run does not deadlock; errors are visible in `/status` and OLED.
- Under sustained load (WiFi + OLED + sensors), logging remains stable or degrades gracefully without capture impact.
