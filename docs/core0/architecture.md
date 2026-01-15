# Core0 Architecture (Nano RP2040 Connect)

Core0 is the **application layer**. It must never participate in time‑critical edge capture. Its job is to:
- ingest Core1’s per‑swing records (`SwingRecordV1`)
- enrich/compute presentation values (seconds, rolling stats)
- poll environmental sensors
- log to SD (raw + stats)
- serve HTTP (and optionally AP)
- drive the OLED display
- host configuration + tunables (persistence + Serial/HTTP interfaces)

Core0 must be **non-blocking** and robust under SD and WiFi variability: if something stalls, we degrade *presentation/logging*, not capture.

---

## Inputs and outputs

### Inputs
- **From Core1:** `SwingRecordV1` via SPSC queue (see `docs/shared/interfaces.md`)
- **Sensors:** temperature, humidity, pressure (SHT41/BMP280, etc.)
- **Config:** persisted settings (WiFi credentials, logging/stats intervals, thresholds)

### Outputs
- **SD card:**
  - Raw CSV: `SwingRecordV1` + env (authoritative)
  - Stats CSV: `StatsRecordV1` at lower cadence (derived)
- **Network:** HTTP endpoints (`/status`, `/latest`, `/stats`, `/files`/downloads, optional `/config`)
- **OLED:** human-facing “latest + summary” screens
- **Diagnostics:** counters for queue overruns, SD errors, WiFi reconnects

---

## The Core0 “center”: `LatestState`

Core0 maintains a single in-memory snapshot that everything reads:
- last `SwingRecordV1`
- derived values for UI and APIs:
  - `period_s` / `period_us` (derived from cycles using the **last-good PPS scale**)
  - `pps_age_ms` (derived from `pps_age_cycles`)
  - rolling aggregates (mean/median, variability proxies)
- quality counters:
  - counts by `gps_state`
  - counts by `flags` bits
  - SD error counters, WiFi reconnect counters
- last environmental sample + timestamp
- current config summary (version, key tunables)

**Rule:** HTTP handlers and OLED rendering read `LatestState`; they do not compute or block.

---

## Stable-first conversion scale (avoid prototype instability)

Core0 may convert cycles to seconds for presentation and stats using a PPS-derived scale:

- Maintain `pps_cycles_last_good` (last-good PPS interval in cycles).
- Update `pps_cycles_last_good` **only when**:
  - `pps_new==1`, and
  - `gps_state == LOCKED`, and
  - PPS passes quality gates (outlier reject / clamp).

Policy:
- In `ACQUIRING` and `BAD_JITTER`: **do not chase** PPS jitter. Hold `pps_cycles_last_good` constant.
- In `HOLDOVER`: continue using the held last-good scale.
- In `NO_PPS` with no last-good scale: derived seconds are invalid (cycles remain valid).

This keeps raw measurement clean and prevents the “fast-but-jittery” acquiring behavior seen in the prototype.

---

## Scheduling model (no RTOS required)

A simple cooperative scheduler in `loop()` is enough.

### High priority (every loop)
- **Drain Core1 queue** quickly and update `LatestState`
  - keep this tight; do not log, poll sensors, or render inside the drain loop

### Periodic jobs (time-sliced)
- **Sensors** (e.g., 1 Hz): poll SHT41/BMP280; update `LatestState.env`
- **OLED** (e.g., 2–10 Hz): render current `LatestState`
- **HTTP** (every loop): handle client requests non-blocking
- **SD logging**
  - raw: append one row per swing (buffered)
  - stats: emit `StatsRecordV1` at cadence (e.g., 10 s) (buffered)
  - flush policy (lines/seconds) and rotation policy (daily/size)
- **Housekeeping** (e.g., 0.2 Hz): free space, heap stats, uptime, etc.

**Design goal:** A slow SD write or WiFi burst should not prevent draining the Core1 queue for long.

---

## Module boundaries (recommended)

### 1) Ingest
- Pop `SwingRecordV1` from queue
- Validate monotonic fields (`swing_id`, `pps_id`)
- Maintain counters and update `LatestState`

### 2) Processing / rolling stats
- Convert cycles to seconds using `pps_cycles_last_good` (stable-first)
- Maintain rolling windows for OLED and `/stats`
- Populate `StatsRecordV1` snapshots

### 3) Sensors
- Init buses, detect devices, poll at cadence
- Handle missing sensors gracefully

### 4) Storage (SD)
- Mount/init SD
- Write raw CSV and stats CSV (buffered, rotated)
- If SD unavailable: continue running and surface error in `/status`

### 5) Networking (WiFi + HTTP)
- WiFi state machine: STA preferred; AP fallback/config if needed
- Endpoints: `/latest`, `/status`, `/stats`, `/files`, `/download`, optional `/config`

### 6) OLED UI
- Render “latest + summary” pages from `LatestState`
- Never block on I/O

### 7) Config + tunables interface
- Persistent config store (version + CRC)
- Serial CLI (minimal): `status`, `get/set`, `save/load/defaults`
- Optional HTTP config portal (`/config`)

---

## Failure policy (important)
- **Capture never depends on SD/WiFi/OLED.**
- If Core0 falls behind:
  - record it (queue health counters)
  - reduce optional work (OLED refresh, stats cadence, HTTP extras)
  - recover automatically (reopen SD, reconnect WiFi)

See `docs/core0/error-policy.md`.

