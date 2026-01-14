# Style guide (unified for the RP2040 rewrite)

**Scope:** Nano RP2040 Connect using **arduino-pico**.

## 1) Naming conventions
- **Types (struct/class/enum):** `PascalCase`
- **Functions & variables:** `lower_snake_case`
- **Constants:** `kPascalCase` (or `constexpr` with descriptive name)
- **Macros:** avoid; if necessary, `MACRO_SCREAMING_SNAKE`

### Units suffix rule (mandatory)
- Prefer `*_cycles` for all Core1 capture timing (single tick domain). Use `*_ticks` only if referencing a peripheral register that is explicitly called “ticks”.
Any value with a unit must be suffixed:
- `_ticks`, `_cycles`, `_us`, `_ms`, `_ns`, `_ppm`, `_ppb`, `_degC`, `_pct`, `_hPa`, etc.
If scaled:
- include scale in name: `_ppm_x1e6`, `_mv_x10`, etc.

## Units (project-wide)
- Core1 internal timebase and all logged swing timing is in **`*_cycles`** (u32) unless explicitly stated otherwise.
- PPS metadata uses `pps_interval_cycles_raw` and `pps_age_cycles`.
- Converted units (`*_us`, `*_ms`, `*_ppm_x1e6`) are produced on Core0 for presentation only.

## 2) File/module layout
- Keep `.ino` files thin: wiring + delegating into modules.
- Code lives under:
  - `src/shared/` (types, queue, config, unit conversions)
  - `src/core1/` (capture + discipline)
  - `src/core0/` (wifi/http/sd/sensors/display/stats)

## 3) Core boundaries
### Core1 rules
- No dynamic allocation.
- No strings or heavy formatting.
- No WiFi, SD, HTTP, sensor I/O.
- Time in **one unit only** (cycles/ticks) throughout.
- All tunables come from `capture_tunables_t` snapshot.

### Core0 rules
- Owns all I/O and “human-facing” outputs.
- Performs unit conversions and attaches environmental data.
- Owns persistence (flash/FS), and publishes tunables to Core1.

## 4) Comments and documentation
- Prefer comments that explain **why**, not what.
- If a comment describes behavior, keep it accurate; stale comments are worse than none.
- Public structs and algorithms must have a short docblock.

## 5) Error handling and diagnostics
- Use explicit status enums and counters (avoid “magic” booleans).
- Any dropped event must increment a counter and be visible on `/status` and logs.
- Prefer “fail soft”:
  - if SD missing → keep running, log error state
  - if WiFi down → keep capture running, serve when available

## 6) Formatting
- Use clang-format if possible; otherwise:
  - 2 spaces indent
  - braces on same line
  - one statement per line
- Keep lines ≤ 100 chars (soft limit).

## 7) JSON/CSV schema rules
- Schema must be versioned:
  - include a `schema_version` field in JSON
  - include a header line with version in CSV logs
- Never silently change units without changing field names and/or schema version.
