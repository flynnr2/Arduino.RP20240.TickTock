# Core0 Architecture (Nano RP2040 Connect)

Core0 is the **application layer**. It must never participate in time‑critical edge capture. Its job is to:
- ingest Core1’s per‑swing records (`SwingRecordV1`)
- enrich/compute presentation values (seconds, ppm/day, rolling stats)
- poll environmental sensors
- log to SD
- serve HTTP (and optionally AP) endpoints
- drive the OLED display

Core0 must be **non-blocking** and robust under SD and WiFi variability: if something stalls, we degrade *presentation/logging*, not capture.

---

## Inputs and outputs

### Inputs
- **From Core1:** `SwingRecordV1` via SPSC queue (see `docs/shared/interfaces.md`)
- **Sensors:** temperature, humidity, pressure (SHT41/BMP280, etc.)
- **Config:** persisted settings (WiFi credentials, logging intervals, display mode, etc.)

### Outputs
- **SD card:** CSV log (append-only; versioned header)
- **Network:** HTTP endpoints (`/status`, `/latest`, `/stats`, `/files`/downloads)
- **OLED:** human-facing “latest + summary” screens
- **Diagnostics:** counters for queue overruns, SD errors, WiFi reconnects

---

## The Core0 “center”: `LatestState`

Core0 should maintain a single in-memory snapshot that everything reads:
- last `SwingRecordV1`
- derived values for UI and APIs:
  - `period_s`, `period_us` (from cycles using current/last-good scale)
  - `rate_ppm`, `rate_s_per_day` (optional)
  - `pps_age_ms` (derived from `pps_age_cycles`)
  - rolling aggregates (mean/median, variability proxies)
- quality counters:
  - counts by `gps_state`
  - ring overflow/dropped/glitch counts (from `flags`)
  - SD error counters, WiFi reconnect counters
- last environmental sample + timestamp

**Rule:** HTTP handlers and OLED rendering read `LatestState`; they do not compute or block.

---

## Scheduling model (no RTOS required)

A simple cooperative scheduler in `loop()` is enough:

### High priority (every loop)
- **Drain Core1 queue** quickly and update `LatestState`
  - keep this tight; do not log or render inside the drain loop

### Periodic jobs (time-sliced)
- **Sensors** (e.g., 1 Hz): poll SHT41/BMP280; update `LatestState.env`
- **OLED** (e.g., 2–10 Hz): render current `LatestState`
- **HTTP** (every loop): call server “tick” / handle client requests non-blocking
- **SD logging**:
  - append one CSV row per `SwingRecordV1`
  - buffer writes; flush every N rows or M seconds
  - rotate file daily or by size
- **Housekeeping** (e.g., 0.2 Hz): update free space, heap stats, uptime, etc.

**Design goal:** A slow SD write or WiFi burst should not prevent draining the Core1 queue for long.

---

## Module boundaries (recommended)

### 1) Ingest
Responsibilities:
- pop `SwingRecordV1` from queue
- validate monotonic fields (`swing_id`, `pps_id` non-decreasing)
- maintain counters (drops/overflows)
- store into `LatestState`

### 2) Processing / derived values
Responsibilities:
- convert cycles to seconds using the current scale
- compute rolling stats (optional, bounded CPU)
- derive display-friendly values (formatted strings, units)

Note: “corr_*” is intentionally omitted from the schema; compute anything needed offline or in Core0 derived stats.

### 3) Sensors
Responsibilities:
- initialize buses (I2C/SPI), detect devices, poll at fixed cadence
- handle missing sensors gracefully (set flags and continue)

### 4) Storage (SD)
Responsibilities:
- mount/init SD
- open log file, write header (schema version, units)
- append rows; flush policy; rotation policy
- handle failure: if SD unavailable, continue running and surface error in `/status`

### 5) Networking (WiFi + HTTP)
Responsibilities:
- WiFi state machine: STA preferred; AP fallback/config page if needed
- HTTP endpoints:
  - `/latest` (JSON): last `SwingRecordV1` + derived values + env
  - `/status` (JSON): counters + health + WiFi/SD state
  - `/stats` (JSON): rolling aggregates, counts by `gps_state`
  - `/files` (HTML/JSON): list log files, download links

### 6) OLED UI
Responsibilities:
- render a small set of “pages” (latest period/rate, gps_state, env, SD/WiFi status)
- page rotation / button input (if present)
- never block on IO

---

## Failure policy (important)
- **Capture never depends on SD/WiFi/OLED.**
- If Core0 falls behind:
  - record it (queue depth/high-water or “missed swings” counter if applicable)
  - keep `/status` truthful
  - recover automatically (e.g., reopen SD file, reconnect WiFi)

---

## Integration contract with Core1
Core0 must treat `SwingRecordV1` as the **truth**:
- `gps_state` is authoritative (5-state enum)
- `pps_new` marks a new PPS interval sample
- `pps_age_cycles` expresses PPS freshness (for holdover UX)

See:
- `docs/shared/interfaces.md`
- `docs/core1/logging-schema.md`
