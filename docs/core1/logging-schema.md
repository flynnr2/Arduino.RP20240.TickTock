# Core1 Logging Schema (Raw CSV)

This document defines the **authoritative** raw swing record schema written by Core0.
Core1 defines the record; Core0 writes it to SD.

Core0 may append environmental sensor fields at the end of each row.

---

## Raw record: `SwingRecordV1` (cycles)

See `docs/shared/interfaces.md` for the struct definition.

### Header (recommended)
Line 1:
- `raw_schema_version=1,units=cycles`

Line 2 (columns, v1):
- `swing_id,pps_id,pps_age_cycles,pps_new,pps_interval_cycles_raw,tick_block_cycles,tick_cycles,tock_block_cycles,tock_cycles,gps_state,flags`

### Env tail (Core0 appends)
If enabled, Core0 appends:
- `temp_c,rh_pct,press_hpa`

If missing sensors, Core0 logs empty fields.

---

## Semantics
- `swing_id` increments once per emitted swing record.
- `pps_id` increments on PPS edges; `pps_new==1` only on the first swing after a PPS edge.
- `pps_interval_cycles_raw` is non-zero only when `pps_new==1` (otherwise 0).
- `gps_state` is the authoritative 5-state enum.
- `flags` is a 16-bit bitfield (see `docs/shared/interfaces.md` appendix).

---

## Forward compatibility
- Unknown columns at the end of a row should be ignored by readers.
- If any field meaning changes, bump `raw_schema_version`.
