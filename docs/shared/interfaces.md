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
This is the **single-row-per-swing** record used for logging and post-processing (see `docs/logging-schema.md`).

```c
typedef enum : uint8_t {
  GPS_NO_PPS = 0,
  GPS_ACQUIRING = 1,
  GPS_LOCKED = 2,
  GPS_HOLDOVER = 3,
  GPS_BAD_JITTER = 4
} gps_state_t;

typedef struct {
  // Alignment / epoch
  uint32_t swing_id;
  uint32_t pps_id;
  uint32_t pps_age_cycles;

  // PPS raw (meaningful when pps_new==1)
  uint32_t pps_interval_cycles_raw;
  uint8_t  pps_new;

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
