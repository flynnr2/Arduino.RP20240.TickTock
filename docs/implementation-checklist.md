# RP2040 TickTock Implementation Checklist

Use this checklist to drive Codex and keep changes incremental and testable.  
**Rule:** docs are the contract — do not invent fields or endpoints outside `docs/shared/interfaces.md` and the core0/core1 docs.

---

## Stage 0 — Contract freeze & scaffolding
- [ ] Confirm platform/toolchain: **Nano RP2040 Connect + arduino-pico**
- [ ] Freeze `SwingRecordV1` and `StatsRecordV1` in `docs/shared/interfaces.md`
- [ ] Freeze raw CSV schema in `docs/core1/logging-schema.md`
- [ ] Confirm flags bit assignments (`docs/shared/interfaces.md` appendix)
- [ ] Establish `src/` layout per docs; avoid duplicate module trees

---

## Stage 1 — Shared module (`src/shared/`)
- [ ] Implement shared types/enums consistent with `docs/shared/interfaces.md`
- [ ] Add schema/version constants (raw_schema_version=1, stats_schema_version=1)
- [ ] Provide string helpers for `gps_state` and `flags` (for `/status`, OLED)

---

## Stage 2 — Core1 plumbing (stub first, then capture)

### Stage 2a — Core1 stub generator (recommended)
- [ ] Implement SPSC queue export of `SwingRecordV1`
- [ ] Generate synthetic `SwingRecordV1` at configurable swing rates
- [ ] Exercise `pps_new/pps_id/pps_age_cycles` patterns + gps_state transitions
- [ ] Confirm Core0 can ingest under SD/WiFi/OLED load before adding PIO/DMA

### Stage 2b — Core1 capture pipeline (`src/core1/`)
- [ ] Implement PIO→DMA→RAM ring capture for pendulum + PPS pins
- [ ] Reconstruct per-swing `SwingRecordV1` and push to queue
- [ ] Maintain authoritative 5-state `gps_state` and set flags bits
- [ ] No Serial printing or heavy work in capture path

---

## Stage 3 — Core0 ingest + `LatestState` (`src/core0/`)
- [ ] Core0 cooperative scheduler (drain queue first)
- [ ] `LatestState` stores last swing record + env + health counters
- [ ] Ingest drains SPSC queue of `SwingRecordV1`
- [ ] Validate monotonic `swing_id`/`pps_id` and track anomalies

---

## Stage 4 — Stable-first scale + conversions (`src/core0/processing/`)
(Defined in `docs/core0/processing.md`)
- [ ] Maintain `pps_cycles_last_good`
- [ ] Update last-good scale **only** when `gps_state==LOCKED` and PPS passes quality gates
- [ ] In `ACQUIRING`/`BAD_JITTER`: hold last-good scale (do not chase)
- [ ] In `NO_PPS` with no last-good: derived seconds invalid (cycles remain valid)
- [ ] Provide helpers for period in seconds/us for OLED and JSON

---

## Stage 5 — Rolling stats (`src/core0/stats/`)
(Windows, robust metrics, and `usable_for_stats` gating are defined in `docs/core0/processing.md`.)
- [ ] Maintain rolling windows in RAM (for OLED + `/stats`)
- [ ] Populate `StatsRecordV1` snapshot (see `docs/shared/interfaces.md`)
- [ ] Ensure bounded CPU/memory; no heap churn in hot paths
- [ ] Use stable-first scale policy for seconds-based stats

---

## Stage 6 — Sensors (`src/core0/sensors/`)
- [ ] SHT41 + BMP280 init/detect
- [ ] Poll cadence (default 1 Hz) and update env cache
- [ ] Missing sensors do not crash; set `FLAG_SENSOR_MISSING`

---

## Stage 7 — SD logging (`src/core0/storage/`)
- [ ] SD init/mount + failure handling
- [ ] **Raw logger**: per swing `SwingRecordV1` + env → raw CSV
- [ ] **Stats logger**: cadence snapshots of `StatsRecordV1` + env → stats CSV
- [ ] Buffered writes + flush policy; no per-line flush
- [ ] Rotation (daily requires time valid; size rotation optional)

---

## Stage 8 — Networking & HTTP (`src/core0/net/`)
- [ ] WiFi STA + optional AP fallback
- [ ] HTTP endpoints:
  - [ ] `/latest` (JSON from `LatestState`)
  - [ ] `/status` (JSON health/counters)
  - [ ] `/stats` (JSON `StatsRecordV1`)
  - [ ] `/files` + `/download` (chunked streaming)
- [ ] Handlers do not do I2C reads; SD reads only in `/download` and chunked

---

## Stage 9 — OLED UI (`src/core0/ui/`)
- [ ] Pages show latest + rolling summary + env + health
- [ ] Render cadence bounded; no allocations in render loop
- [ ] OLED failures disable UI and set `FLAG_OLED_ERROR`

---

## Stage 10 — Config + tunables (`src/core0/config/`)
- [ ] Persistent config store (version + CRC)
- [ ] Minimal Serial CLI: `status`, `get/set`, `save/load/defaults`, `help`
- [ ] Optional `/config` endpoint mirrors Serial keys
- [ ] Expose config version/reset count in `/status`

---

## Stage 11 — Integration & verification
- [ ] Stress harness: Core1 stub + Core0 SD/WiFi/OLED/sensors all enabled
- [ ] Long run with representative swing rates:
  - [ ] no queue overflows
  - [ ] raw + stats logs sane
  - [ ] `/latest` and `/status` responsive
- [ ] Swap Core1 stub → real PIO/DMA capture and repeat stress run
