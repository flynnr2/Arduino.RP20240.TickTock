# RP2040 Dual‑Core Pendulum Timer — Documentation

This folder captures the **design contract** for the Nano RP2040 Connect rewrite:
- **Core1**: time‑critical capture + PPS discipline (PIO→DMA→RAM ring), produces **one record per swing**.
- **Core0**: everything else — sensors, SD logging, HTTP/AP, OLED UI, and higher‑level processing.

The intent is to keep Core1 “capture‑pure” and make Core0 an application layer that **ingests** Core1 output, **enriches**, **persists**, and **presents** it.

---

## Directory layout

### `docs/core1/` — Capture + discipline
- `capture-timestamping.md` — unified PIO/DMA approach for pendulum edges + PPS
- `logging-schema.md` — the authoritative `SwingRecordV1` schema (raw-first, **cycles**)
- `test-plan.md` — capture health + integration tests

### `docs/core0/` — Application layer
- `architecture.md` — Core0 responsibilities, modules, scheduling, failure policy

### `docs/shared/` — Shared contracts & conventions
- `interfaces.md` — shared structs, queues, and contracts (Core1 ↔ Core0)
- `style.md` — naming, units, conventions (project-wide)

### Root docs
- `platform-choice.md` — platform decision (arduino-pico) and capture pipeline choice
- `Existing_System_Map_and_RP2040_Strategy.md` — mapping of the current two‑MCU system and the RP2040 reimplementation plan

---

## Key locked decisions (summary)
- **Arduino core:** **arduino-pico** (Earle Philhower RP2040 core)
- **Capture:** unified **PIO→DMA→RAM ring** for **pendulum edges and PPS**
- **One timing unit:** `*_cycles` (Core1 capture tick domain; wrap-safe unsigned math)
- **Logging:** **one row per swing** (`SwingRecordV1`) with reconstructable PPS (`pps_new`, `pps_interval_cycles_raw`)
- **State:** `gps_state` is authoritative: `NO_PPS / ACQUIRING / LOCKED / HOLDOVER / BAD_JITTER`
- **No raw edge stream (v1):** Core1 exports only `SwingRecordV1` (raw edge debug may be added later if needed)

---

## How to use these docs
1. Read **`platform-choice.md`** for the platform/capture decisions.
2. Read **`docs/shared/interfaces.md`** for the data contract between cores.
3. Read **`docs/core1/*`** to implement Core1.
4. Read **`docs/core0/architecture.md`** to implement Core0 modules.
5. Use **`docs/core1/test-plan.md`** to validate capture health under load.

