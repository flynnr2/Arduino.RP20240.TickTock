Below is a **Codex-ready Core0 task breakdown** that maps directly from the current UNO “other stuff” to the new RP2040 Core0 architecture (queue ingest + `LatestState` hub + SD/WiFi/OLED/sensors). Each task has: **goal, files, key behaviors, acceptance tests**.

You can drop this straight into a `docs/core0/tasks.md` if you want.

---

# Core0 Implementation Tasks (RP2040 / arduino-pico)

## Task 0 — Core0 skeleton + scheduler loop

**Goal:** Create Core0 entrypoint and a cooperative scheduler that never blocks Core1 ingest.

**Files**

* `src/core0/core0_main.cpp`
* `src/core0/scheduler.h/.cpp` (or keep scheduler inline)

**Behaviors**

* `setup()` initializes modules (config, ingest, sensors, storage, net, oled).
* `loop()`:

  * drains Core1 queue first (tight loop, bounded per iteration optional)
  * runs periodic jobs (sensors, OLED, SD flush, housekeeping)
  * runs network tick every loop (non-blocking)

**Acceptance**

* Compiles on Nano RP2040 Connect (arduino-pico).
* With a fake “record generator” (or Core1 stub), Core0 continues to ingest without stalling while WiFi/SD/OLED are enabled.
* No long blocking calls inside the ingest drain path.

---

## Task 1 — Shared state hub: `LatestState`

**Goal:** Define a single in-memory “truth” object that all presentation/logging reads from.

**Files**

* `src/core0/state/latest_state.h/.cpp`
* `src/common/types.h` (already exists: `SwingRecordV1`, `gps_state_t`, flags)

**Behaviors**

* Stores last `SwingRecordV1`
* Stores derived values for UI/API:

  * `period_us` or `period_s` (derived on Core0)
  * `pps_age_ms` (derived)
  * counters: counts by `gps_state`, flags counts, SD/WiFi health
* Thread model: Core0-only ownership, updated by ingest, read by others

**Acceptance**

* Unit-like compile check: all modules depend only on `LatestState` and don’t duplicate parsing/derivations.
* `LatestState` can be printed/serialized without touching hardware.

---

## Task 2 — Ingest: drain Core1 SPSC queue of `SwingRecordV1`

**Goal:** Replace `NanoComm` with shared-memory ingest.

**Files**

* `src/core0/ingest/swing_ingest.h/.cpp`
* `src/common/ring_spsc.h` (if not already)
* `src/shared/interfaces.h` → referenced but not modified

**Behaviors**

* Drain queue, update `LatestState`:

  * last record
  * monotonic checks: `swing_id` must increase; `pps_id` non-decreasing
  * update counters from `flags`
  * count `gps_state` occurrences
* Tracks queue health (optional): “max drained per loop”, “missed swings” estimate if detectable

**Acceptance**

* Under synthetic load (e.g., 60 swings/sec), Core0 drains without overflow for hours (assuming Core1 is healthy).
* Counters increase appropriately when input flags injected (e.g., ring overflow).

---

## Task 3 — Processing: conversions + lightweight rolling stats

**Goal:** Provide derived values for OLED and HTTP (not heavy analysis).

**Files**

* `src/core0/processing/conversions.h/.cpp`
* `src/core0/processing/aggregates.h/.cpp` (optional but useful)
* `src/common/time_utils.h` (wrap-safe helpers)

**Behaviors**

* Convert `SwingRecordV1` raw cycles into:

  * period in seconds/us (using current/last-good scale if/when you implement it)
  * `pps_age_ms` from `pps_age_cycles`
* Optional rolling stats:

  * rolling mean/median period
  * variability proxy (MAD, IQR, stddev over last N)
  * counts by `gps_state`

**Acceptance**

* Deterministic computation (no heap churn, bounded CPU).
* No dependency on SD/WiFi/OLED.
* Produces stable values even through `HOLDOVER/NO_PPS` (by using raw cycles and freshness markers).

---

## Task 4 — Sensors: environmental polling + cache

**Goal:** Port the UNO `Sensors` behavior into Core0.

**Files**

* `src/core0/sensors/sensors.h/.cpp`
* `src/core0/sensors/sht41.h/.cpp`
* `src/core0/sensors/bmp280.h/.cpp` (or combined)

**Behaviors**

* Initialize I2C, detect sensors, record availability flags
* Poll at fixed cadence (e.g., 1 Hz)
* Update `LatestState.env` (T/H/P + timestamp)
* Failure handling: if sensor missing, keep running and expose status

**Acceptance**

* Runs with either sensor missing (no crash).
* Values available via OLED and HTTP endpoints.
* Poll cadence stable and non-blocking.

---

## Task 5 — Storage: SD mount + CSV logger + rotation + flush policy

**Goal:** Replace `SDLogger` + “CSV header from Nano” with direct `SwingRecordV1` logging.

**Files**

* `src/core0/storage/sd_manager.h/.cpp`
* `src/core0/storage/csv_logger.h/.cpp`
* `src/core0/storage/file_rotate.h/.cpp` (optional)

**Behaviors**

* SD init/mount with robust error reporting
* Create log file (daily or size-based rotation)
* Write header including:

  * `schema_version=1,units=cycles`
  * column headers matching `docs/core1/logging-schema.md`
* Append one row per swing:

  * `SwingRecordV1` fields + optional env fields (if you want in same CSV)
* Buffer writes; flush every N lines or M seconds
* If SD fails: continue running; surface error in `/status`

**Acceptance**

* Produces a valid CSV readable by your post-processing suite.
* Rotation works (new file at midnight or size threshold).
* Pulling SD mid-run doesn’t deadlock; SD removal is visible in `/status`.

---

## Task 6 — Networking: WiFi (STA/AP) + HTTP server routes

**Goal:** Port the UNO `HttpServer` capabilities in a cleaner “LatestState serialization” design.

**Files**

* `src/core0/net/net_manager.h/.cpp`
* `src/core0/net/http_server.h/.cpp`
* `src/core0/net/routes_*.h/.cpp` (optional split)
* `src/core0/config/config_store.h/.cpp` (WiFi creds etc.)

**Behaviors**

* WiFi:

  * STA preferred (connect using stored creds)
  * optional AP fallback/config portal if STA fails
* HTTP endpoints (minimum useful set):

  * `/latest` (JSON): last swing record + derived values + env
  * `/status` (JSON): counters, `gps_state` counts, SD/WiFi health, uptime
  * `/stats` (JSON): rolling aggregates if implemented
  * `/files` (HTML or JSON): list log files
  * `/download?name=...` stream file
  * (optional) `/wifi` GET/POST for config portal

**Acceptance**

* With SD+OLED running, HTTP remains responsive.
* `/latest` always returns quickly and never blocks on SD.
* WiFi reconnect logic works after AP/router bounce.

---

## Task 7 — OLED UI: pages driven by `LatestState`

**Goal:** Port the UNO `Display` module but make it purely “render from LatestState”.

**Files**

* `src/core0/ui/oled.h/.cpp`
* `src/core0/ui/pages.h/.cpp`

**Behaviors**

* Splash screen + runtime pages
* Page set (suggested):

  * Timing: latest period/rate, `gps_state`, PPS age
  * Environment: T/H/P
  * Health: SD ok?, WiFi status, drops/overflows counters
* Refresh cadence (e.g., 2–10 Hz)
* No I/O in render path except the display write

**Acceptance**

* Never blocks ingest noticeably (no long redraw loops).
* Pages update correctly with live data and state transitions.

---

## Task 8 — Config store: persistent settings (WiFi creds + app tunables)

**Goal:** Replace UNO EEPROM config logic with RP2040-friendly persistent storage.

**Files**

* `src/core0/config/config_store.h/.cpp`
* `src/core0/config/config_defaults.h`

**Behaviors**

* Store/load:

  * WiFi SSID/pass
  * logging mode (on/off, rotate daily/size)
  * sensor poll interval, display page mode
* Version + CRC
* Safe defaults on corruption

**Acceptance**

* Corrupt config falls back to defaults.
* Changing WiFi creds persists across reboot.
* Config version bump doesn’t brick previous installs.

---

## Task 9 — Diagnostics + health counters surfaced everywhere

**Goal:** Replace `MemoryMonitor` and unify health reporting.

**Files**

* `src/core0/diag/health.h/.cpp`
* integrate into `/status` and OLED health page

**Behaviors**

* Track:

  * queue drain counts, missed/overrun indicators
  * SD errors, last flush time, bytes written
  * WiFi status, reconnect count
  * heap/free RAM, uptime
* Export to:

  * `/status`
  * OLED health page

**Acceptance**

* Health stays truthful under failures (SD removed, WiFi down).
* No “silent” failure modes.

---

## Task 10 — Integration test harness on-device

**Goal:** A single “integration sketch mode” that proves Core0 is robust under load.

**Files**

* `src/core0/test/integration_mode.h/.cpp` (or build flag in `core0_main.cpp`)

**Behaviors**

* Option A: if Core1 not ready, generate synthetic `SwingRecordV1` at configurable rate.
* Stress:

  * SD writing enabled
  * HTTP polled continuously
  * OLED refreshing
  * sensors polling
* Record stability: zero queue overflows at target rates

**Acceptance**

* Runs for 12+ hours at representative swing rates without capture/ingest degradation.
* Produces log files, HTTP responsive, OLED updates.

---

## Suggested execution order (minimize rework)

1. Task 0–2 (skeleton + LatestState + ingest)
2. Task 5 (SD logging) + Task 6 (HTTP `/latest` + `/status`)
3. Task 4 (sensors) + Task 7 (OLED)
4. Task 8 (config) + STA/AP behavior
5. Task 3 (derived stats) + Task 9/10 (diagnostics + stress harness)

---
