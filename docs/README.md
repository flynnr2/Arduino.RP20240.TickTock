# RP2040 TickTock — Documentation

This folder defines the plan and data contracts for the Nano RP2040 Connect dual-core rewrite.

**Core idea**
- **Core1**: time-critical capture + GPS/PPS discipline; exports `SwingRecordV1` only.
- **Core0**: application layer: ingest, rolling stats, sensors, SD logging (raw + stats), WiFi/HTTP, OLED, config/tunables.

---

## Key documents

### Shared contract
- `shared/interfaces.md` — authoritative structs (`SwingRecordV1`, `StatsRecordV1`), enums, flags bits

### Core1
- `core1/capture-timestamping.md` — PIO→DMA capture strategy
- `core1/logging-schema.md` — raw CSV schema (Core0 writes)
- `core1/test-plan.md` — capture and correctness tests

### Core0
- `core0/processing.md` — DSP rules: stable-first scale, sample gating, windows, robust stats, sanity checks, wrap policy
- `core0/architecture.md` — module boundaries and stable-first policy
- `core0/tasks.md` — Codex-ready work breakdown
- `core0/storage.md` — SD logging strategy (raw + stats)
- `core0/networking.md` — HTTP endpoints (`/latest`, `/status`, `/stats`, `/files`, `/download`)
- `core0/config.md` — persistence + Serial CLI + optional `/config`
- `core0/error-policy.md` — graceful degradation rules

### Project plan
- `implementation-checklist.md` — staged Codex plan

---

## Logging overview

### Raw log (authoritative)
One row per swing:
- `SwingRecordV1` (cycles) + env snapshot

### Stats log (derived)
Lower cadence snapshots:
- `StatsRecordV1` + env snapshot

Raw is ground truth for post-processing. Stats is convenience for OLED/telemetry.

---

## Design guardrails
- Capture must not depend on SD/WiFi/OLED/sensors.
- Core0 conversions use a **stable-first** PPS scale: update only when `gps_state==LOCKED`; otherwise hold last-good.
- HTTP handlers read `LatestState` only; `/download` streams SD files chunked.
