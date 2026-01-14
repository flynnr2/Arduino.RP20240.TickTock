# Logging schema (preferred): single combined record (one row per swing)

This is the **authoritative** logging format for the RP2040 dual-core rewrite.

Capture is unified **PIO→DMA→RAM ring** for pendulum + PPS; the log stores the resulting per-swing records.

## Goals
- **Raw-first:** store raw swing timing in **cycles** (u32), not converted units.
- **Alignment:** include PPS ID + freshness so post-processing can reconstruct the PPS stream and detect transitions.

## Record: `SwingRecordV1` (one row per swing)

### Swing raw (cycles)
- `tick_block_cycles` (u32)
- `tick_cycles` (u32)
- `tock_block_cycles` (u32)
- `tock_cycles` (u32)

### Alignment / epoch
- `swing_id` (u32, monotonic)
- `pps_id` (u32, last PPS edge seen; monotonic)
- `pps_age_cycles` (u32, cycles since last PPS edge)  
  *(Alternative: `pps_age_ms` if you prefer, but cycles is preferred.)*

### PPS raw (meaningful when `pps_new==1`)
- `pps_interval_cycles_raw` (u32, cycles between consecutive PPS edges)
- `pps_new` (u8/bool 0–1) — **1 only on the first swing after a PPS edge**  
  Enables extracting the true PPS series from swing rows.

### State / flags
- `gps_state` (u8 enum; see below)
- `flags` (u16 bitfield; see below)


---

## `gps_state` enum (authoritative)
- `0 NO_PPS`
- `1 ACQUIRING`
- `2 LOCKED`
- `3 HOLDOVER`
- `4 BAD_JITTER`

### Precedence rules (avoid ambiguity)
1. If PPS is absent → `HOLDOVER` if a last-good scale exists, else `NO_PPS`.
2. If PPS is present → `BAD_JITTER` if quality too poor; else `ACQUIRING` until stable; else `LOCKED`.


---

## `flags` bitfield (initial proposal)
- bit 0: `FLAG_DROPPED` — one or more events/samples dropped since last record
- bit 1: `FLAG_GLITCH` — invalid edge pattern / reconstruction reset
- bit 2: `FLAG_CLAMP` — correction/scale clamp applied
- bit 3: `FLAG_RING_OVERFLOW` — DMA ring overrun occurred
- bit 4: `FLAG_PPS_OUTLIER` — PPS interval rejected as outlier (Hampel/median)
- bits 5–15: reserved

---

## CSV header (recommended)
Version everything explicitly:

`schema_version=1,units=cycles`

Then:

`swing_id,pps_id,pps_age_cycles,pps_new,pps_interval_cycles_raw,tick_block_cycles,tick_cycles,tock_block_cycles,tock_cycles,gps_state,flags`

Notes:
- When `pps_new==0`, log `pps_interval_cycles_raw=0` (preferred).
