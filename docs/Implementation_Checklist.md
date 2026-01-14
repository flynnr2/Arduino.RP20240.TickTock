# RP2040 TickTock Implementation Checklist

Use this checklist to drive staged development and keep the implementation aligned with the
approved design contract and constraints.

## Stage 0 — Contract freeze & scaffolding
- [ ] Validate `SwingRecordV1` field list, order, and units (`*_cycles`).
- [ ] Validate `gps_state_t` enum values and precedence rules.
- [ ] Confirm `flags` bit assignments and ownership (Core1 vs Core0).
- [ ] Lock CSV header/versioning format (`schema_version=1,units=cycles`).
- [ ] Confirm HTTP endpoints and response schemas are doc-aligned (`/latest`, `/status`, `/stats`, `/files`).

## Stage 1 — Shared module (`src/shared/`)
- [ ] Define shared types (`SwingRecordV1`, `gps_state_t`, flags).
- [ ] Implement SPSC queue for Core1 → Core0.
- [ ] Document SPSC memory ordering rules (single producer/consumer only).
- [ ] Ensure unit naming conventions (`*_cycles`, `*_us`, `*_ppm_x1e6`).

## Stage 2 — Core1 capture pipeline (`src/core1/`)
- [ ] Implement PIO program to capture pendulum + PPS edges into packed words.
- [ ] Implement DMA ring buffer and overrun accounting.
- [ ] Expand packed words into edge events with tie handling.
- [ ] Reconstruct swings using the fixed edge state machine; emit one record per swing.
- [ ] Implement PPS interval measurement in the same tick domain.
- [ ] Enforce `pps_new==1` only on the first swing after a PPS edge.
- [ ] Emit `SwingRecordV1` to SPSC queue (no dynamic allocation).
- [ ] Maintain capture diagnostics counters (drops, ring overflow, glitches).

## Stage 3 — Core0 ingest + `LatestState` (`src/core0/`)
- [ ] Implement high-priority queue drain loop (no logging/rendering in drain loop).
- [ ] Maintain `LatestState` with last record, derived values, counters, and env cache.
- [ ] Validate monotonic `swing_id`/`pps_id` and track anomalies.

## Stage 4 — Core0 processing & rolling stats (`src/core0/stats/`)
- [ ] Convert cycles → seconds/us on Core0 only.
- [ ] Compute rolling stats (bounded CPU) outside the ingest path.
- [ ] Populate derived values for UI/HTTP (period, rate, pps_age_ms).

## Stage 5 — Sensors (`src/core0/sensors/`)
- [ ] Initialize I2C/SPI buses and detect sensors.
- [ ] Poll sensors at fixed cadence; cache results in `LatestState`.
- [ ] Surface missing sensor status via flags/counters.

## Stage 6 — SD logging (`src/core0/logging/`)
- [ ] Mount/init SD and open log file.
- [ ] Write versioned CSV header.
- [ ] Append one row per `SwingRecordV1`.
- [ ] Buffer writes + flush/rotation policy.
- [ ] Surface SD errors via flags/counters; keep capture running.

## Stage 7 — Networking & HTTP (`src/core0/net/`)
- [ ] Implement WiFi state machine (STA preferred, AP fallback if needed).
- [ ] Implement `/latest`, `/status`, `/stats`, `/files` endpoints.
- [ ] Ensure handlers only read `LatestState` (no sensor/SD work).
- [ ] Implement `/download` streaming with SD access only in that handler.

## Stage 8 — OLED UI (`src/core0/display/`)
- [ ] Render pages (period/rate, gps_state, env, SD/WiFi status).
- [ ] Page rotation/button input without blocking.

## Stage 9 — Integration & verification
- [ ] DMA ring health test (no overruns at expected rates).
- [ ] PPS/pendulum tie handling test (stable reconstruction).
- [ ] PPS stream reconstruction test (`pps_new`, `pps_id` monotonic).
- [ ] `gps_state` precedence tests (NO_PPS/ACQUIRING/LOCKED/HOLDOVER/BAD_JITTER).
- [ ] Load test under WiFi+SD to confirm no capture degradation.
