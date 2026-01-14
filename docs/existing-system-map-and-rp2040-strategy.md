# Existing system map + RP2040 dual‑core reimplementation strategy
*(Source: ZIPs you provided — **Arduino.Pendulum.Timer** (Nano Every) + **Arduino.Pendulum.Timer.Display** (UNO R4 WiFi). No new code written yet.)*

## Decisions (locked)
- **Target board:** Nano RP2040 Connect
- **Arduino core:** **arduino-pico** (Earle Philhower RP2040 core)
- **Capture pipeline:** **Unified PIO edge capture + DMA → RAM ring** for **both** pendulum sensor edges and GPS PPS edges
- **Time unit on Core1:** a **single tick domain** derived from the PIO capture timebase (no unit switching)
- **Collision policy:** simultaneous PPS + pendulum edges may share the same timestamp; **ties are valid**
- **Core split:** Core1 = capture + PPS discipline; Core0 = WiFi/HTTP/SD/sensors/display/stats + all conversions


## 1) Entry points (what boots where)

### 1.1 Nano Every (time‑critical)
- **Sketch:** `Nano.Every/Nano.Every.ino`
  - `setup()` → `pendulumSetup()` (after two 5s delays + LED blink)
  - `loop()`  → `pendulumLoop()`

**Key modules**
- `Nano.Every/src/PendulumCore.cpp` — ISRs, edge ring buffers, swing reconstruction, PPS disciplining, sample production
- `Nano.Every/src/CaptureInit.cpp` — configures EVSYS + TCB0/1/2 hardware for capture
- `Nano.Every/src/SerialParser.cpp` — command parsing (`get/set/stats/help`) + CSV streaming
- `Nano.Every/src/EEPROMConfig.*` — tunables persistence
- `Nano.Every/src/PendulumProtocol.h` — shared CSV schema + tunable parameter names + units tags

### 1.2 UNO R4 WiFi (everything else)
- **Sketch:** `Uno.R4/Uno.R4.ino`
  - `setup()` initializes: Serial, Wire, `NANO_SERIAL`, EEPROM config, WiFi, HTTP, SD, sensors, display
  - `loop()` services:
    - `WiFiConfig::service()`, `HttpServer::service()`, `SDLogger::service()`
    - `Sensors::poll()`
    - read line(s) from Nano → `NanoComm::parseLine()` → attach env → `StatsEngine::update()` → `SDLogger::logSample()`
    - OLED update (1 Hz)

**Key modules**
- `Uno.R4/src/NanoComm.cpp` — parses Nano CSV (tags + fields), converts to ticks, builds SD header on missing nano header
- `Uno.R4/src/StatsEngine.*` — rolling statistics, derived metrics for UI/HTTP
- `Uno.R4/src/SDLogger.*` — file mgmt + write header + append records + service flush/rotation
- `Uno.R4/src/WiFiConfig.*` + `WiFiStorage.*` — AP/STA behavior + EEPROM credential slots
- `Uno.R4/src/HttpServer.*` + `ArduinoHttpServer.*` — endpoints (HTML + JSON) + nano tunables web UI
- `Uno.R4/src/Display.*` — OLED rendering + scrolling log
- `Uno.R4/src/Sensors.*` — environmental sensor polling + cached “latest”
- `Uno.R4/src/EEPROMConfig.*` — tunables persistence (UNO + Nano params)

---

## 2) Hardware/timing capture (Nano Every) — what’s “true” in the current implementation

### 2.1 Pin + event routing
From `Nano.Every/src/CaptureInit.cpp`:
- **IR sensor generator:** `PORTB.PIN0` (PB0) → **EVSYS.CHANNEL1** → `EVSYS.USERTCB1` (TCB1 capture)
- **GPS PPS generator:** `PORTD.PIN0` (PD0) → **EVSYS.CHANNEL2** → `EVSYS.USERTCB2` (TCB2 capture)

### 2.2 Counters
- `TCB0`: free‑running base time (16‑bit + overflow ISR → coherent 32‑bit time in TCB0 domain)
- `TCB1`: capture edges (IR) via EVSYS, ISR timestamps edge into TCB0 domain
- `TCB2`: capture PPS edges via EVSYS, ISR timestamps PPS into TCB0 domain

### 2.3 IR edge ISR behavior (important subtlety)
From `Nano.Every/src/PendulumCore.cpp` `ISR(TCB1_INT_vect)`:
- Reads `TCB1.CCMP` (captured) and `TCB1.CNT` (now)
- Computes edge time in TCB0 domain via `edge32 = now32 - latency16`
- **Toggles which edge is captured next** by flipping `TCB1.EVCTRL` EDGE bit each time  
  → yields alternating edge sequence with `EdgeEvent.type` = 0 then 1 then 0 …

> Note: several comments label “rising/falling” inconsistently vs the actual use; the *behavior* (alternating edge capture) is what matters.

### 2.4 Swing reconstruction state machine
`process_edge_events()` consumes `EdgeEvent` and reconstructs a `FullSwing` in the expected pattern:
- type 0 → type 1 → type 0 → type 1 → type 0
- states:
  - **0** wait for first type 0 (start)
  - **1** end `tick_block` on type 1
  - **2** end `tick`       on type 0
  - **3** end `tock_block` on type 1
  - **4** end `tock`       on type 0 → push `FullSwing`

Output `FullSwing` fields (all in TCB0 ticks):
- `tick_block`, `tick`, `tock_block`, `tock`

### 2.5 PPS capture ISR
`ISR(TCB2_INT_vect)` timestamps PPS into TCB0 ticks and pushes into a PPS ring buffer; sets a flag for the main loop to process.

---

## 3) GPS disciplining + correction outputs (Nano Every)

### 3.1 Main algorithm location
- `Nano.Every/src/PendulumCore.cpp` → `process_pps()`

### 3.2 Filtering stages (as implemented)
High-level stages inside `process_pps()`:
1. **Raw PPS delta** (`delta_raw`) from consecutive PPS timestamps (TCB0 ticks)
2. **Outlier conditioning**:
   - Hampel filter (window `Tunables::ppsHampelWin`, threshold `Tunables::ppsHampelKx100` × MAD)
   - optional median-of-3 (`Tunables::ppsMedian3`)
3. **Two EWMA tracks**
   - `pps_delta_fast` updated with `Tunables::ppsFastShift`
   - `pps_delta_slow` updated with `Tunables::ppsSlowShift`
4. **Quality metrics**
   - **R** (drift proxy): `|fast - slow| / slow` mapped to ppm
   - **J** (jitter proxy): `MAD / slow` mapped to ppm
5. **State machine**
   - internal states include `NO_PPS`, `ACQUIRING`, `LOCKED`, `HOLDOVER`, `BAD_JITTER` (exact names in code)
   - lock/unlock thresholds use `Tunables::ppsLock*`, `ppsUnlock*`, `ppsUnlockCount`, `ppsHoldoverMs`
6. **Blend selection**
   - blend weight based on R between `ppsBlendLoPpm` and `ppsBlendHiPpm`
   - forces fully-slow when locked, fully-fast when acquiring (per code logic)

### 3.3 Outputs (legacy; not in new RP2040 schema)
The Nano exports:
- `corr_inst_ppm`  (instantaneous correction) — **ppm × 1e6**
- `corr_blend_ppm` (blended correction) — **ppm × 1e6**
- `dropped_events` count

These are computed from `pps_delta_*` relative to `F_CPU` in code.

---

## 4) Serial protocol between boards (current)

### 4.1 Data lines (Nano → UNO)
From `Nano.Every/src/SerialParser.cpp` `sendSample()`:
- CSV line prefix tag is `dataUnitsTag(Tunables::dataUnits)` where `Tunables::dataUnits` ∈:
  - `RawCycles` → tag `16Mhz`
  - `AdjustedUs` → tag `uSec`
  - `AdjustedNs` → tag `nSec`
  - `AdjustedMs` → tag `mSec`
- Data fields (always 8 fields after the tag):


### 4.2 Header + status lines
- Header line starts with `TAG_HDR` and provides field names matching the units mode.
- Status line starts with `TAG_STS` and carries code/text.

### 4.3 Commands (UNO → Nano)
UNO issues commands primarily from the `/nano` HTTP endpoint:
- `set <param> <value>` using names defined in `PendulumProtocol.h`
UNO implementation: `Uno.R4/src/HttpServer.cpp` → `nanoCommand()` prints to `NANO_SERIAL` and waits up to 200 ms for one newline response.

Nano responds on `CMD_SERIAL` (mapped to `Serial1`), printing either the value (for get/set) or an error string.

---

## 5) RP2040 dual-core rewrite: mapping existing responsibilities → new cores

## 5.1 Core split (clean, per your requirements)
### Core 1 (Capture Core)
Port from Nano Every:
- Edge/PPS timestamping (but using RP2040 facilities)
- Edge event ring buffer + swing state machine
- PPS disciplining + `corr_{inst,blend}_ppm`
- Publish **samples** into an SPSC queue (shared RAM)

**Strict rule:** Core1 keeps time in **one unit only** (recommend: *cycles* or *one consistent tick unit*). No “16MHz vs µs vs ns” switching; no conversions.

### Core 0 (App Core)
Port from UNO R4:
- WiFi/AP provisioning, HTTP endpoints, OLED UI
- SD logging + rotation
- Sensors + attaching env fields
- StatsEngine / derived metrics
- All conversions and presentation formatting

## 5.2 Shared-memory interface (replaces serial)

### A) SPSC queue: Core1 → Core0 (single combined record; one row per swing)
We will emit a **single combined record per swing**, **raw-first**, in **cycles**. This replaces the old “SwingRecordV1” concept and **omits** the `corr_*` fields (they can be computed in post-processing).

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
  uint32_t swing_id;        // monotonic
  uint32_t pps_id;          // last PPS edge seen; monotonic
  uint32_t pps_age_cycles;  // cycles since last PPS edge (freshness)

  // PPS raw (meaningful when pps_new==1)
  uint32_t pps_interval_cycles_raw; // cycles between PPS edges
  uint8_t  pps_new;                 // 1 only on first swing after PPS edge

  // Swing raw (cycles)
  uint32_t tick_block_cycles;
  uint32_t tick_cycles;
  uint32_t tock_block_cycles;
  uint32_t tock_cycles;

  // State / flags
  uint8_t  gps_state;   // gps_state_t (authoritative)
  uint16_t flags;       // bitfield: dropped, glitch, clamp, ring overflow, PPS outlier, ...
} SwingRecordV1;
```

**Notes**
- Core0 logs `SwingRecordV1` directly (CSV) and may also enrich into a “latest snapshot” for UI/HTTP.

### B) Shared config: Core0 → Core1
Replace the `set/get` serial commands with a versioned struct:


```c
struct CaptureTunables {
  float     correctionJumpThresh;
  uint8_t   ppsFastShift, ppsSlowShift;
  uint8_t   ppsHampelWin;
  uint16_t  ppsHampelKx100;
  bool      ppsMedian3;
  uint16_t  ppsBlendLoPpm, ppsBlendHiPpm;
  uint16_t  ppsLockRppm, ppsLockJppm;
  uint16_t  ppsUnlockRppm, ppsUnlockJppm;
  uint8_t   ppsUnlockCount;
  uint16_t  ppsHoldoverMs;
  uint32_t  version; // Core0 increments; Core1 applies when version changes
};
```

---

## 6) Unifying style + fixing known issues going forward
### 6.1 Style convention proposal (apply everywhere)
- Types: `PascalCase`
- Functions/vars: `lower_snake_case`
- Constants: `kConstantName` (or `constexpr` + descriptive name)
- Units always explicit in identifier: `*_ticks`, `*_cycles`, `*_us`, `*_ppm_x1e6`, etc.

### 6.2 Known inconsistencies (explicitly “don’t get bogged down”)
- SD header inconsistencies: treat as legacy; rewrite cleanly with versioned schema

---

## 7) RP2040 timestamping approach (chosen)

### Why the Nano Every EVSYS approach changes on RP2040
The Nano Every used **EVSYS → TCB capture** for hardware timestamps. RP2040 has no EVSYS equivalent, so we will use **PIO** as the hardware capture engine.

### Chosen design: unified PIO + DMA ring for *all* edges
- A PIO state machine captures **both** pendulum sensor edges and PPS edges.
- DMA drains the PIO RX FIFO continuously into a **RAM ring buffer**.
- Core1 consumes the ring, reconstructs swings, and computes `pps_cycles` from PPS events.

**Benefits:** deterministic timestamps, minimal CPU coupling, robust under WiFi/SD/HTTP load, and “PPS collides with breakbeam edge” becomes a deterministic tie (or 1‑tick separation), not ISR jitter.

### Tick resolution guidance
- Choose a fixed tick rate so quantization is well below your error budget (often **1 µs or better** is plenty for pendulum work; faster ticks provide margin).
- Use unsigned wrap-safe subtraction everywhere: `dt = (uint32_t)(t2 - t1)`.

### Data handling
- Core1 outputs per-swing `SwingRecordV1` records into an SPSC sample queue.
- Core0 performs unit conversions (seconds/us/ns, ppm scaling cleanup), logging, UI, and networking.


## 8) Codex-ready task breakdown (no coding yet)

### Task 0 — Platform constraints (already decided)
Deliverable: `docs/platform-choice.md`
- **Arduino core:** **arduino-pico** (locked)
- **Capture:** unified **PIO→DMA→RAM ring** for pendulum + PPS (locked)
- Phase‑0 validation: WiFi/AP + HTTP + SD logging stability while capture runs; confirm no drops at expected rates

### Task 1 — Produce a canonical “existing behavior spec” from the ZIPs
Deliverable: `docs/existing-behavior-spec.md`
- Exact edge sequence assumptions (type 0/1 mapping)
- Exact PPS disciplining math + state transitions
- Exact field list + types used in logging/HTTP

### Task 2 — Define shared types + queues
Deliverable: `src/shared/`
- `types.h` (`SwingRecordV1`, `gps_state_t`, flags, enums)
- `spsc_queue.h` (single-producer/single-consumer ring buffer)
- `capture_tunables.h` (versioned config struct)
- Document memory ordering rules (SPSC only).

### Task 3 — Core1: unified capture + reconstruction + PPS discipline
Deliverable: `src/core1/`
- **PIO program** to monitor pendulum + PPS pins and emit packed edge events
- **DMA** into RAM ring buffer + overrun accounting
- Ring consumer → expand events → `process_edge_events()` (same swing state machine)
- PPS events → compute `pps_cycles` → run existing dual-track smoothing (fast/slow) + lock state
- Push `SwingRecordV1` to SPSC sample queue
- Diagnostics: ring high-water mark, DMA overruns, dropped sample count, PPS quality metrics

### Task 4 — Core0: consume samples + attach sensors + stats
Deliverable: `src/core0/`
- queue consumer
- `Sensors` port and integration
- `StatsEngine` port (units made explicit)
- maintain “latest snapshot” for display + HTTP

### Task 5 — SD logging rewrite (clean header)
Deliverable: `src/core0/logging/`
- versioned header line (explicit units)
- resilient rotation + append mode
- don’t replicate legacy header hacks

### Task 6 — WiFi/AP + HTTP port (mostly reuse UNO R4 concepts)
Deliverable: `src/core0/net/`
- AP provisioning flow and credential persistence
- endpoints equivalent to UNO:
  - `/`, `/json`, `/wifi`, `/nano` (rename `/capture`), `/stats`, `/stats.json`, `/logfiles`, `/download`

### Task 7 — Integration + verification mode
Deliverable: `tools/verify/`
- optional “legacy CSV output mode” so you can diff RP2040 output against known-good runs
- acceptance checklist:
  - swing metrics plausible and stable
  - PPS correction magnitudes sane
  - no drift regressions vs current two-board system
  - no event loss under WiFi+SD load

---

## 9) Notes / extension points
- The Nano Every design has excellent “hardware timestamp → main-loop reconstruction” separation; keep that separation.
- The biggest simplification in RP2040 rewrite is eliminating serial framing and all unit-tag parsing.
- Keep the PPS disciplining code “as is” initially, then fix scaling/units issues once correctness is confirmed.

## Logging schema (chosen)
We will log a **single combined record per swing**, raw-first, in **cycles**:
- swing raw: `tick_block_cycles`, `tick_cycles`, `tock_block_cycles`, `tock_cycles`
- alignment: `swing_id`, `pps_id`, `pps_age_cycles`
- PPS raw: `pps_interval_cycles_raw` with `pps_new` flag (1 only on first swing after PPS)
See `docs/core1/logging-schema.md`.
