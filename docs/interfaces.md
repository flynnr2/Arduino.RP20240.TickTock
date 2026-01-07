# Shared interfaces (Core1 ↔ Core0)


## Note on acquisition
- Raw edge acquisition (if enabled) uses a **unified PIO→DMA→RAM ring** for both pendulum and PPS pins.
- Core1 expands packed ring words into edge events and/or per-swing samples.

## Raw edge event format (optional)
```c
typedef enum : uint8_t { SRC_PENDULUM = 0, SRC_PPS = 1 } edge_src_t;
typedef enum : uint8_t { EDGE_FALL = 0, EDGE_RISE = 1 } edge_pol_t;
typedef struct {
  uint32_t t_ticks;   // PIO tick timebase (wrap OK)
  uint8_t  src;       // edge_src_t
  uint8_t  pol;       // edge_pol_t
  uint8_t  flags;     // overflow/tie/etc.
  uint8_t  reserved;
} edge_event_t;
```
**Tie policy:** if PPS and pendulum change in the same tick, Core1 emits two events with identical `t_ticks` (or derives two from one packed word).
