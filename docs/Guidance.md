# RP2040 TickTock Implementation Guidance (Reiterate Every Stage)

Use this as a short, persistent reminder before starting any stage from the checklist.

## Progress protocol
- Work **stage by stage** from `docs/Implementation_Checklist.md`.
- Stop after each stage for review before proceeding.
- Update `docs/STATUS.md` after each stage (current stage, completed items, next up, blockers).

## Non-negotiable constraints
- **Docs are contract; do not invent fields**
- **No heap allocation in the ingest path.**
- **No I2C/SD inside HTTP handlers** (except `/download` streaming).
- **No blocking delays in the scheduler** (cooperative loop only).
- **Stick to doc-defined endpoints and schemas** (`SwingRecordV1`, `/latest`, `/status`, `/stats`, `/files`).

## Core boundaries
- **Core1:** capture + discipline only (no strings/heavy formatting, no I/O, no dynamic allocation).
- **Core0:** all I/O and derived values only (no capture involvement).

## Schema and units
- **Core1 uses cycles only**; convert on Core0.
- **Versioned CSV/JSON schemas** (no silent changes).

## Review prompts (per stage)
- Is the ingest path allocation-free and non-blocking?
- Are HTTP handlers free of sensor/SD operations (except `/download`)?
- Are we aligned with the documented schemas and endpoints?
- Are Core0 computations outside the ingest loop?
