# Core0 Tasks

This is a Codex-ready breakdown of Core0 work, aligned to `docs/core0/architecture.md` and the shared contract in `docs/shared/interfaces.md`.

Core0 is the application layer: ingest Core1 records, compute stable derived values and rolling stats, log to SD (raw+stats), serve HTTP, poll sensors, drive OLED, and manage configuration/tunables.

---

## Task 0 — Core0 skeleton + cooperative scheduler
**Goal:** Core0 main loop that never blocks Core1 ingest.

**Deliverables**
- `src/core0/core0_main.cpp`
- `src/core0/scheduler.*` (optional)

**Acceptance**
- Compiles for Nano RP2040 Connect (arduino-pico).
- Queue drain is always executed first; no SD/WiFi/OLED/I2C calls inside drain.

---

## Task 1 — Shared state hub: `LatestState`
**Goal:** A single in-memory “truth” object for UI/API/logging.

**Deliverables**
- `src/core0/state/latest_state.*`

**Must include**
- last `SwingRecordV1`
- last env sample
- rolling aggregates and a current `StatsRecordV1`
- health counters (flags counts, SD/WiFi/sensor status)
- current `pps_cycles_last_good` + “scale valid” indicator

---

## Task 2 — Ingest: drain Core1 SPSC queue of `SwingRecordV1`
**Goal:** Replace legacy serial parsing with shared-memory ingest.

**Deliverables**
- `src/core0/ingest/swing_ingest.*`

**Behavior**
- Validate monotonic `swing_id`/`pps_id`; track anomalies.
- Update `LatestState` and counters.

**Acceptance**
- Under synthetic load (e.g., 60 swings/sec), no queue overflows with all other modules enabled.

---

## Task 3 — Stable-first scale + conversions
**Goal:** Convert cycles → seconds only when safe, avoiding prototype instability.

**Deliverables**
- `src/core0/processing/scale.*`
- `src/core0/processing/conversions.*`

**Policy (must)**
- Update `pps_cycles_last_good` only when `pps_new==1` AND `gps_state==LOCKED` AND PPS passes quality gates.
- In `ACQUIRING`/`BAD_JITTER`, hold last-good scale; do not chase.
- In `NO_PPS` without last-good, mark derived seconds invalid.

---

## Task 4 — Rolling stats engine (in RAM) + `StatsRecordV1`
**Goal:** Maintain rolling windows for OLED and `/stats`, and emit `StatsRecordV1` snapshots.

**Deliverables**
- `src/core0/stats/stats_engine.*`

**Acceptance**
- Bounded CPU and memory; no heap churn in hot paths.
- Uses stable-first scale policy for seconds-based stats.

---

## Task 5 — Sensors: environmental polling + cache
**Deliverables**
- `src/core0/sensors/*`

**Acceptance**
- Missing sensors do not crash the system; status reflects missing devices.

---

## Task 6 — SD logging: raw + stats
**Goal:** Two separate CSV logs.

**Deliverables**
- `src/core0/storage/raw_logger.*` (one row per swing: `SwingRecordV1` + env)
- `src/core0/storage/stats_logger.*` (cadenced: `StatsRecordV1` + env)
- `src/core0/storage/sd_manager.*` (mount, rotate, flush policy)

**Acceptance**
- Raw log schema matches `docs/core1/logging-schema.md` + env tail.
- Stats log schema matches `StatsRecordV1` in `docs/shared/interfaces.md`.
- SD removal mid-run does not deadlock; flags/counters indicate failure.

---

## Task 7 — Networking & HTTP
**Deliverables**
- `src/core0/net/*`

**Endpoints (baseline)**
- `/latest` (JSON) from `LatestState`
- `/status` (JSON)
- `/stats` (JSON `StatsRecordV1`)
- `/files`, `/download`

**Acceptance**
- Handlers never touch I2C; SD reads only inside `/download` and chunked.

---

## Task 8 — OLED UI
**Deliverables**
- `src/core0/ui/oled.*`

**Acceptance**
- Reads only `LatestState`.
- Shows latest + rolling summary + health.

---

## Task 9 — Config store + tunables interface
**Goal:** Persist and edit configuration safely.

**Deliverables**
- `src/core0/config/config_store.*` (version + CRC)
- `src/core0/config/cli_serial.*` (minimal CLI)
- optional `/config` endpoint

**Acceptance**
- Corrupt config falls back to defaults with a visible reset counter.
- `get/set/save/load/defaults` works over Serial.

---

## Task 10 — Integration stress harness
**Goal:** Prove ingest remains healthy under SD + WiFi + OLED load.

**Deliverables**
- `src/core0/test/integration_mode.*` (or build flag)

**Acceptance**
- Runs for hours at representative swing rates with no queue overflows and sane logs.
