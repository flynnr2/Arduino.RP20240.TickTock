# Shared interfaces (Core1 ↔ Core0)

This document defines the **shared-memory contract** between the Capture Core (Core1) and the App Core (Core0).

## Design principles
- **Single producer, single consumer (SPSC)** for the sample stream:
  - Producer: Core1
  - Consumer: Core0
- No dynamic allocation in Core1.
- Core1 writes only “timing truth” + discipline outputs; Core0 enriches (env, stats) and logs.

---

## A) Sample stream (Core1 → Core0)

### Core1-produced payload (minimal)
```c
typedef enum : uint8_t {
  GPS_NO_PPS = 0,
  GPS_ACQUIRING = 1,
  GPS_LOCKED = 2
} gps_status_t;

typedef struct {
  uint32_t tick_ticks;
  uint32_t tock_ticks;
  uint32_t tick_block_ticks;
  uint32_t tock_block_ticks;

  // Correction factors exported for reporting/conversions on Core0.
  // Convention: ppm × 1e6 (integer transport). Define in one place and stick to it.
  int32_t corr_inst_ppm_x1e6;
  int32_t corr_blend_ppm_x1e6;

  uint16_t dropped_events;
  uint8_t  gps_status;

  uint32_t sample_seq;     // monotonically increments (wrap allowed)
  uint64_t t0_ticks;        // optional: capture timebase at sample creation (debug/trace)
} sample_core1_t;
```

### Queue semantics
- Core1 pushes **exactly one** `sample_core1_t` per completed full swing.
- Core0 pops as fast as possible, but may decimate for display.
- If the queue is full:
  - Core1 increments `dropped_events` and drops the sample (or overwrites oldest; pick one and document it).

---

## B) Shared configuration (Core0 → Core1)

### Versioned tunables block
Core0 is the source of truth for tunables (loaded from persistent storage, UI changes, HTTP updates).

```c
typedef struct {
  // PPS smoothing / filtering
  uint8_t  pps_fast_shift;
  uint8_t  pps_slow_shift;
  uint8_t  pps_hampel_win;
  uint16_t pps_hampel_k_x100;
  bool     pps_median3;

  // Blend + lock thresholds (ppm units, *not* scaled unless named)
  uint16_t pps_blend_lo_ppm;
  uint16_t pps_blend_hi_ppm;

  uint16_t pps_lock_r_ppm;
  uint16_t pps_lock_j_ppm;
  uint16_t pps_unlock_r_ppm;
  uint16_t pps_unlock_j_ppm;
  uint8_t  pps_unlock_count;
  uint16_t pps_holdover_ms;

  // Edge/swing validation (units are Core1 ticks)
  uint32_t min_edge_sep_ticks;
  uint32_t max_edge_sep_ticks;
  uint32_t correction_jump_thresh_ppm; // if used, define clearly

  // Versioning
  uint32_t version;       // Core0 increments on any change
  uint32_t crc32;         // optional; compute over struct (excluding crc field)
} capture_tunables_t;
```

### Apply rules
- Core1 periodically checks `version` (e.g., once per loop iteration).
- On change:
  - Copy the entire struct to Core1 local storage (atomic copy pattern).
  - Validate ranges; if invalid, reject and set a diagnostic flag in Core1 state.

---

## C) Memory ordering + atomicity

### Queue implementation requirements
- Use a classic SPSC ring buffer with:
  - `head` written by producer
  - `tail` written by consumer
- Ensure appropriate compiler barriers / atomics so Core0 sees fully-written samples:
  - Write sample payload → then publish by advancing `head`.

### Recommended approach
- Use `std::atomic<uint32_t>` for head/tail (C++), or platform-provided barriers.
- Keep the queue in a dedicated translation unit with tests.

---

## D) Optional: Event stream (future refinement)
If you later want *raw edge events* for offline analysis:
- Add a second SPSC queue of `edge_event_t` items:
  - timestamp + edge type + channel + flags
- Keep it optional/compile-time gated to avoid overhead in normal runs.
