# Shared interfaces (Core1 ↔ Core0)


## Note on acquisition
- Raw edge acquisition (if enabled) uses a **unified PIO→DMA→RAM ring** for both pendulum and PPS pins.
- Core1 expands packed ring words into edge events and/or per-swing samples.

## Raw edge event format (optional)
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
**Tie policy:** if PPS and pendulum change in the same tick, Core1 emits two events with identical `t_cycles` (or derives two from one packed word).

## Preferred combined swing record (logging + Core0 processing)
This is the **single-row-per-swing** record used for logging and post-processing (see `docs/core1/logging-schema.md`).

```c
typedef enum : uint8_t {
  NO_PPS     = 0,
  ACQUIRING  = 1,
  LOCKED     = 2,
  HOLDOVER   = 3,
  BAD_JITTER = 4
} gps_state_t;

typedef struct {
  // Alignment / epoch
  uint32_t swing_id;
  uint32_t pps_id;
  uint32_t pps_age_cycles;

  // PPS raw (meaningful when pps_new==1)
  uint8_t  pps_new;
  uint32_t pps_interval_cycles_raw;

  // Swing raw (cycles)
  uint32_t tick_block_cycles;
  uint32_t tick_cycles;
  uint32_t tock_block_cycles;
  uint32_t tock_cycles;

  // State / flags
  uint8_t  gps_state;   // gps_state_t (authoritative)
  uint16_t flags;
} SwingRecordV1;
```

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
| 5 | `FLAG_PPS_MISSING` | PPS expected but not observed within horizon for this swing (freshness exceeded threshold); typically accompanies `HOLDOVER/NO_PPS`. | Core1 |
| 6 | `FLAG_TIME_INVALID` | Core0 time-of-day not valid (no NTP/time source yet); affects filename rotation and timestamps (not swing cycles). | Core0 |
| 7 | `FLAG_SD_ERROR` | SD logging currently unavailable or write failure detected (Core0 continues running). | Core0 |
| 8 | `FLAG_WIFI_DOWN` | WiFi currently disconnected/unavailable (STA and/or AP). | Core0 |
| 9 | `FLAG_SENSOR_MISSING` | One or more configured environmental sensors missing or not responding. | Core0 |
| 10 | `FLAG_OLED_ERROR` | OLED init/write failure; UI disabled or degraded. | Core0 |
| 11–15 | reserved | Reserved for future use; must be written as 0 and ignored by readers. | — |

### Notes
- **Core1-originated bits** (0–5) describe capture/discipline quality and are the primary ones used in post-processing.
- **Core0-originated bits** (6–10) describe application-layer health and are primarily used for UI/HTTP `/status`.
- Readers must treat unknown bits as “ignore” (forward-compatible).
- If a bit’s semantics change, bump the **schema_version** in the CSV header.
