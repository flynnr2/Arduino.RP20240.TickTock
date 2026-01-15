# Shared interfaces (Core1 ↔ Core0)

This document defines the **data contract** between Core1 (capture/discipline) and Core0 (application layer).
Treat it as authoritative: do not add fields ad-hoc without bumping schema versions and updating docs.

---

## Timing units
- Core1 capture timebase uses a single tick domain recorded as `*_cycles` (wrap-safe unsigned subtraction).
- Core0 may convert cycles to seconds **only for presentation/derived stats** using the last-good PPS scale.

---

## GPS/PPS state (authoritative)

Core1 emits a single authoritative `gps_state` for each swing:

```c
typedef enum : uint8_t {
  NO_PPS     = 0,  // never seen PPS or PPS absent beyond horizon; no valid epoch
  ACQUIRING  = 1,  // PPS present but not yet stable enough to trust
  LOCKED     = 2,  // PPS present and stable; trusted regime
  HOLDOVER   = 3,  // PPS absent, but last-good scale exists and is being held
  BAD_JITTER = 4,  // PPS present but too noisy/out-of-family to trust
} gps_state_t;
```

**Precedence rules (summary):**
1. PPS absent → `HOLDOVER` if last-good scale exists, else `NO_PPS`
2. PPS present → `BAD_JITTER` if quality too poor; else `ACQUIRING` until stable; else `LOCKED`

---

## Raw edge event format (optional)

Core1 may internally expand PIO/DMA ring words into edge events; v1 does **not** require exporting edges to Core0.
If enabled later for diagnostics, the format is:

```c
typedef enum : uint8_t { SRC_PENDULUM = 0, SRC_PPS = 1 } edge_src_t;
typedef enum : uint8_t { EDGE_FALL = 0, EDGE_RISE = 1 } edge_pol_t;

typedef struct {
  uint32_t t_cycles;  // PIO capture tick domain (wrap OK)
  uint8_t  src;       // edge_src_t
  uint8_t  pol;       // edge_pol_t
  uint8_t  flags;     // overflow/tie/etc.
  uint8_t  reserved;
} edge_event_t;
```

**Tie policy:** if PPS and pendulum change in the same tick, Core1 records two events with identical `t_cycles`
(or derives two from one packed word). Downstream logic must accept ties.

---

## `SwingRecordV1` (one row per swing)

This is the **primary record** exported from Core1 to Core0 and written to the **raw CSV**.

```c
typedef struct {
  // Alignment / epoch
  uint32_t swing_id;       // monotonic
  uint32_t pps_id;         // last PPS edge seen (monotonic; increments on PPS edges)
  uint32_t pps_age_cycles; // cycles since last PPS edge (freshness)

  // PPS raw (meaningful when pps_new==1)
  uint8_t  pps_new;                // 1 only on the first swing after a PPS edge
  uint32_t pps_interval_cycles_raw; // cycles between PPS edges (0 when pps_new==0)

  // Swing raw (cycles)
  uint32_t tick_block_cycles;
  uint32_t tick_cycles;
  uint32_t tock_block_cycles;
  uint32_t tock_cycles;

  // State / flags
  uint8_t  gps_state;  // gps_state_t (authoritative)
  uint16_t flags;      // bitfield; see appendix
} SwingRecordV1;
```

Notes:
- Core1 exports **only `SwingRecordV1`** in v1 (no raw edge stream).
- Readers reconstruct a clean PPS series using `(pps_new, pps_id, pps_interval_cycles_raw)`.

---

## `StatsRecordV1` (derived rolling statistics)

Core0 maintains rolling statistics in RAM (for OLED + `/stats`) and may write a **separate stats CSV** at a lower cadence.
`StatsRecordV1` is derived and may be updated/extended independently of the raw record (bump `stats_schema_version`).

```c
typedef struct {
  // Timebase for the stats sample
  uint32_t uptime_ms;      // millis() at stats emission
  uint32_t swing_id_last;  // last swing_id included
  uint32_t pps_id_last;    // last pps_id included

  // Window definition
  uint32_t window_swings;  // number of swings in window
  uint32_t window_ms;      // approximate wall time spanned by window (optional; 0 if unknown)

  // Current regime
  uint8_t  gps_state;      // gps_state_t (current/most recent)

  // Scale used for derived seconds (0 if unavailable)
  uint32_t pps_cycles_last_good;  // last-good PPS interval cycles used for conversions

  // Derived timing (for UI/telemetry; not used as raw truth)
  double   period_mean_s;         // mean period (seconds)
  double   period_mad_s;          // median absolute deviation (seconds) or 0 if not computed
  double   period_std_s;          // std dev (seconds) or 0 if not computed

  // Counts by gps_state within the window (optional but useful)
  uint16_t count_locked;
  uint16_t count_acquiring;
  uint16_t count_holdover;
  uint16_t count_bad_jitter;
  uint16_t count_no_pps;

  // Aggregated flags within the window (bitwise OR)
  uint16_t flags_or;
} StatsRecordV1;
```

Notes:
- Core0 must update `pps_cycles_last_good` **only when `gps_state == LOCKED` and PPS passes quality gates**.
- In `ACQUIRING`/`BAD_JITTER`, Core0 should **hold** the last-good scale (stable measurement > fast lock).

---

## Appendix: `flags` bit assignments

`SwingRecordV1.flags` is a 16‑bit bitfield used to surface capture/processing health without adding additional record types.

### Bit layout (v1)

| Bit | Name | Meaning | Set by |
|---:|---|---|---|
| 0 | `FLAG_DROPPED` | One or more events/samples dropped since the previous emitted swing record (e.g., queue/ring pressure). | Core1 |
| 1 | `FLAG_GLITCH` | Unexpected/invalid edge pattern during swing reconstruction (forced resync/reset). | Core1 |
| 2 | `FLAG_CLAMP` | Disciplining/clamp logic engaged (e.g., PPS interval or scale clamped/rejected but operation continued). | Core1 |
| 3 | `FLAG_RING_OVERFLOW` | DMA ring overrun / unread data overwritten (capture kept running but data loss occurred). | Core1 |
| 4 | `FLAG_PPS_OUTLIER` | PPS interval sample rejected as outlier (e.g., Hampel/median gate); `gps_state` may move to `BAD_JITTER`. | Core1 |
| 5 | `FLAG_PPS_MISSING` | PPS expected but not observed within horizon for this swing; typically accompanies `HOLDOVER/NO_PPS`. | Core1 |
| 6 | `FLAG_TIME_INVALID` | Core0 time-of-day not valid (no NTP/time source yet); affects daily rotation and timestamps (not swing cycles). | Core0 |
| 7 | `FLAG_SD_ERROR` | SD logging unavailable or write failure detected (Core0 continues running). | Core0 |
| 8 | `FLAG_WIFI_DOWN` | WiFi disconnected/unavailable (STA and/or AP). | Core0 |
| 9 | `FLAG_SENSOR_MISSING` | One or more configured sensors missing/not responding. | Core0 |
| 10 | `FLAG_OLED_ERROR` | OLED init/write failure; UI disabled or degraded. | Core0 |
| 11–15 | reserved | Reserved for future use; must be written as 0 and ignored by readers. | — |

### Notes
- Core1-originated bits (0–5) describe capture/discipline quality and are the primary ones used in post-processing.
- Core0-originated bits (6–10) describe application-layer health and are primarily used for UI/HTTP `/status`.
- Readers must treat unknown bits as “ignore” (forward-compatible).
- If a bit’s semantics change, bump the relevant schema version(s).
