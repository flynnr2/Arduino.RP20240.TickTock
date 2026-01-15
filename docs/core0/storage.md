# Core0 Storage (SD Logging)

Core0 owns SD card initialization, file management, and logging. Logging must degrade gracefully: SD failure must not affect capture or ingest.

Core0 produces **two** logs:
1. **Raw log** (authoritative): one row per swing, `SwingRecordV1` + contemporaneous environmental readings.
2. **Stats log** (derived): lower-cadence rolling summaries (`StatsRecordV1`) for OLED/telemetry convenience.

---

## Goals
- Append-only CSV logging with stable schemas.
- Versioned headers for post-processing.
- Robustness to SD absence/removal and slow writes.
- No blocking of Core1 ingest.

---

## File layout and naming
Recommended:
- Directory: `/logs/`
- Raw file:
  - Daily: `logs/raw/YYYY-MM-DD.csv`
  - or size-rotated: `logs/raw/runNNN_partMMM.csv`
- Stats file:
  - Daily: `logs/stats/YYYY-MM-DD.csv`
  - or size-rotated: `logs/stats/runNNN_partMMM.csv`

Choose one rotation policy at a time (configurable), applied consistently to both logs.

---

## Time validity (for daily rotation)
Daily rotation requires “time-of-day valid”.

Define time validity as:
- **Valid** after at least one successful NTP time acquisition, and
- **Stays valid** until the last sync exceeds `time_valid_max_age_s` (config; e.g. 6–24h).

Before time is valid:
- use uptime-based filenames (e.g., `logs/raw/uptime-<ms>.csv`), and
- set `FLAG_TIME_INVALID` in `/status` and OLED.

---

## Raw CSV (`SwingRecordV1` + env)

### Header (recommended)
Line 1 (metadata):
- `raw_schema_version=1,units=cycles`

Line 2 (columns):
- `swing_id,pps_id,pps_age_cycles,pps_new,pps_interval_cycles_raw,tick_block_cycles,tick_cycles,tock_block_cycles,tock_cycles,gps_state,flags,temp_c,rh_pct,press_hpa`

Env fields are the **contemporaneous cached** readings from Core0; if missing, log empty fields.

### Row semantics
- When `pps_new==0`, log `pps_interval_cycles_raw=0`.
- `gps_state` is authoritative 5-state enum.
- `flags` is a bitfield; see `docs/shared/interfaces.md`.

---

## Stats CSV (`StatsRecordV1`)

The stats log is optional but recommended for quick inspection and OLED-friendly telemetry.

### Cadence
- Write at a fixed cadence (config), e.g. every **10 s** (good default).
- OLED uses the in-RAM rolling stats updated every swing; the stats CSV is just a periodic snapshot.

### Header (recommended)
Line 1 (metadata):
- `stats_schema_version=1`

Line 2 (columns) (CSV order for v1):
- `uptime_ms,swing_id_last,pps_id_last,window_swings,window_ms,gps_state,pps_cycles_last_good,period_mean_s,period_mad_s,period_std_s,count_locked,count_acquiring,count_holdover,count_bad_jitter,count_no_pps,flags_or,temp_c,rh_pct,press_hpa`

Env fields here may be:
- last sample at emission time, or
- window-averaged (choose one and keep consistent; default: “last sample”).

---

## Write and flush policy
- Raw log: append one row per ingested swing record.
- Stats log: append one row per cadence tick.
- Buffer writes and flush:
  - every **N lines** (e.g., 50–200), or
  - every **M seconds** (e.g., 2–5s),
  whichever occurs first.
- Avoid per-line `flush()` calls.

### Blocking limits
- SD writes can be slow; do not block ingest for long.
- If logging falls behind:
  - keep ingesting and updating `LatestState`,
  - set `FLAG_SD_ERROR` and increment SD error counters,
  - resume logging when SD recovers.

---

## Rotation policy
Pick one (config-controlled):
1. **Daily rotation** (requires time valid)
2. **Size rotation** (roll when file exceeds threshold, e.g. 25–200 MB)

Rotation should roll both raw and stats logs together.

---

## Failure handling
- SD missing at boot:
  - set `sd_available=false`,
  - keep running; `/status` reports SD unavailable.
- SD removed mid-run:
  - detect write/open failure, close file(s), set error counters,
  - continue running with logging disabled until reinserted (optional re-mount attempt).
- Corruption:
  - never overwrite existing logs; open new file if needed.

---

## HTTP integration
Storage supports:
- list log files for `/files`
- streaming download for `/download?name=...` (read-only; chunked)

---

## Acceptance checks
- Produces CSVs compatible with post-processing:
  - raw = authoritative `SwingRecordV1 + env`
  - stats = `StatsRecordV1` snapshots
- Rotation works as configured.
- Removing SD mid-run does not deadlock; errors are visible in `/status` and OLED.
- Under sustained load (WiFi + OLED + sensors), logging remains stable or degrades gracefully without capture impact.


---

## Stats computation reference
Stats are defined in `docs/shared/interfaces.md` (`StatsRecordV1`) and computed according to `docs/core0/processing.md` (windows, robust statistics, usability gating).

---

## Appendix: Logging metadata and sampling notes
### Log header metadata (recommended)
Include a metadata line that helps long experiments remain reproducible:
- `fw_version=...`
- `git_hash=...` (if available)
- `build_time_utc=...`

Example (raw):
- `raw_schema_version=1,units=cycles,fw_version=...,git_hash=...,build_time_utc=...`

### Environmental sampling note
Env fields appended to raw/stats rows are the **latest cached** sensor readings at the time the row is written.
