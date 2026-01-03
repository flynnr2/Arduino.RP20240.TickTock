# Arduino Pendulum Timer

High-precision pendulum timing system split across two microcontrollers:

- **Arduino Nano Every (ATmega4809):** hard‑real‑time edge capture, swing assembly, PPS discipline, CSV/meta output, runtime tunables via serial.
- **Arduino UNO R4 WiFi:** logging to SD, simple HTTP UI/API, environment sensors (BMP280, SHT4x), Wi‑Fi credential management with AP fallback, rolling statistics and metrics.

> **Repo status:** This repository currently ships only the **UNO R4 WiFi** firmware. The Nano Every firmware sources will be added later.

> **Note on buffers:** Previous drafts mentioned a `SwingBuffer` type. The current codebase uses **fixed-size ring buffers** instead: `evbuf` (edge events), `swing_buf` (assembled swings), and `ppsBuffer` (PPS captures). Sizes are power‑of‑two for fast masking. See `Config.h` and `PendulumCore.cpp`.

---

## Contents

- [Architecture](#architecture)
- [Hardware & Wiring](#hardware--wiring)
- [Firmware Layout](#firmware-layout)
- [Quick Start](#quick-start)
- [Dependencies](#dependencies)
- [Serial Console (Nano Every)](#serial-console-nano-every)
- [Tunables](#tunables)
  - [Nano Every](#nano-every-tunables)
  - [UNO R4 WiFi](#uno-r4-wifi-tunables)
- [Data & CSV Schema](#data--csv-schema)
- [HTTP Endpoints (UNO R4 WiFi)](#http-endpoints-uno-r4-wifi)
- [Accuracy & PPS Discipline](#accuracy--pps-discipline)
- [Timing Resolution & Quantization Error] (timing-resolution--1uantization-error)
- [Diagnostics & Troubleshooting](#diagnostics--troubleshooting)
- [Build Notes](#build-notes)
- [License](#license)

---

## Architecture

**Data path overview**

1. **IR sensor → Nano Every**: The IR break‑beam (or reflective IR) signal feeds **PB0** on the Nano Every.
2. **Event capture**: PB0 is routed via **EVSYS** to:
   - **TCB1** for **rising** edges (tick),
   - **TCB2** for **falling** edges (tock),
   while **TCB0** free‑runs to provide extended timestamps (overflow extension to 32‑bit time).
3. **Swing assembly**: Edge events in `evbuf` are paired/validated into `swing_buf`.
4. **PPS discipline**: GPS PPS captures are buffered in `ppsBuffer` and used to calibrate tick→time scaling (EMA filter).
5. **Output**: The Nano prints CSV/meta lines over UART.
6. **UNO R4**: Receives Nano CSV/meta, appends environment readings (temperature, humidity, pressure), writes to SD, hosts a small HTTP UI/API, and shows rolling stats on its display / endpoints.

**Roles**

- **Nano Every**
  - Time‑critical edge capture & timestamping
  - PPS‑based discipline and correction factor
  - Assembles swings, applies optional scaling to chosen units
  - Serial command parser for tunables
- **UNO R4 WiFi**
  - SD logging (CSV with header)
  - BMP280/SHT4x sampling
  - Wi‑Fi provisioning (AP fallback if STA connect fails)
  - Minimal web server with JSON/status endpoints
  - Rolling statistics & metrics

---

## Hardware & Wiring

- **IR sensor output → Nano Every PB0**.
- **GPS PPS → Nano Every**: If using the current default wiring, connect PPS to **PD0**, as configured in `PendulumCore.cpp`.
- **UNO R4 WiFi**
  - SD card (onboard) formatted **FAT32**
  - **BMP280** to I²C
  - **SHT4x** to I²C

> The Nano’s PB0 edges are routed by **EVSYS** to **TCB1 (rising)** and **TCB2 (falling)** for input capture; **TCB0** runs free as the overflow/timebase counter.

If you modify pins, update the corresponding defines and EVSYS/TCB setup in the Nano firmware.

---

## Firmware Layout

```
/Uno.R4
  ├─ src/
  │   ├─ NanoComm.cpp/.h         # Read Nano lines, manage CSV header/units, append env data
  │   ├─ HttpServer.cpp/.h       # Minimal HTTP server & endpoints
  │   ├─ Sensors.cpp/.h          # BMP280 & SHT4x sampling
  │   ├─ Metrics.cpp/.h          # Rolling statistics, windows
  │   └─ Config.h                # Tunables & settings
  └─ Uno.R4.ino
```

> **Note:** Nano Every firmware sources are not yet included in this repository.

---

## Quick Start

1. **Install board packages**
   - *Arduino Renesas Mbed OS Boards* (UNO R4 WiFi)
2. **Install libraries (UNO R4 WiFi)**
   - `Adafruit_BMP280`
   - `Adafruit_SHT4x`
   - SD support (UNO R4 built‑in SD)
3. **Build & flash**
   - Flash **`Uno.R4/Uno.R4.ino`** to the **UNO R4 WiFi**.
   - Nano Every firmware is not included in this repository.
4. **First boot / Wi‑Fi**
   - If no credentials are stored, the UNO starts in **AP mode**. Connect and browse to `/wifi` to set SSID/password.
   - On subsequent boots, the UNO attempts **STA**; if connection fails it **falls back to AP** automatically.
5. **SD logging**
   - Insert a **FAT32** SD card. The UNO will create a CSV and write the header plus subsequent samples.

---

## Dependencies

**Boards**
 - **Arduino Renesas Mbed OS Boards** (UNO R4 WiFi)

**Libraries (UNO R4)**
- `Adafruit_BMP280`
- `Adafruit_SHT4x`
- (Uses UNO R4 built‑in SD support)

**Serial link**
- Nano ↔ UNO default baud: **115200** (`SERIAL_BAUD_NANO`).

---

## Serial Console (Nano Every)

> Nano Every firmware sources are not included in this repository. The following interface applies when that firmware is available.

```
Commands:
  help
      Show command & tunables help.

  stats
      Print running metrics (drops, buffer fill, EMA status, etc.).

  get <param>
      Read a tunable (e.g., get dataUnits).

  set <param> <value>
      Set a tunable (e.g., set dataUnits adjusted_us).
```

---

## Tunables

### Nano Every Tunables (EEPROM-persistent)

> Requires the Nano Every firmware, which is not included in this repository.

| Name                  | Purpose / Effect                                                   | Default | Notes |
|-----------------------|--------------------------------------------------------------------|---------|-------|
| `dataUnits`           | Output units: `raw_cycles`, `adjusted_ms`, `adjusted_us`, `adjusted_ns` | `adjusted_us` | Changes CSV scaling emitted by Nano. |
| `ppsEmaShift`         | EMA shift for PPS correction factor                                | `4`     | Higher shift = slower response, smoother. |
| `correctionJumpThresh`| Threshold to ignore large PPS correction jumps                     | `50`    | Protects against bad PPS samples. |

### UNO R4 WiFi Tunables

| Name                  | Purpose / Effect                                   | Default | Notes |
|-----------------------|----------------------------------------------------|---------|-------|
| `statsWindowSize`     | Number of samples in rolling stats                  | `100`   | Affects `/stats` endpoint. |
| `rollingWindowMs`     | Time window for metrics aggregation                | `60000` |      |
| `blockJumpUs`         | Threshold for marking a block jump (us)            | `500`   |      |
| `debounceTicks`       | Debounce threshold in timer ticks                   | `5`     |      |
| `ppsMinUs` / `ppsMaxUs` | PPS width bounds                                  | `950000` / `1050000` | Rejects invalid PPS pulses. |
| `metricsPeriodMs`     | Metrics computation period                          | `1000`  |      |
| `minEdgeSepTicks`     | Minimum allowed separation between edges            | `50`    | Drop suspicious events. |
| `txBatchSize`         | Lines to batch per SD write                         | `10`    |      |
| `ringSize`            | UNO-side ring buffer length (lines)                 | `512`   |      |
| `ppsEmaShift`         | UNO-side PPS EMA shift                              | `4`     |      |
| `dataUnits`           | Expected units from Nano                            | Auto    | Set from first meta line. |
| Flags                 | `protectSharedReads`, `enableMetrics`| Off     | Debug/testing aids. |

---

## Data & CSV Schema

**Units**

- The **Nano** can switch output units at runtime via:
  ```
  set dataUnits <raw_cycles|adjusted_ms|adjusted_us|adjusted_ns>
  ```
- The **UNO** auto‑detects the Nano’s current units from the first meta/header line.

**CSV header**

```
<nano_header>,temperature_C,humidity_pct,pressure_hPa
```
- `<nano_header>` is emitted by the Nano and defines swing/edge fields in the selected units.

---

## HTTP Endpoints (UNO R4 WiFi)

| Path     | Purpose                                  |
|----------|------------------------------------------|
| `/`      | Home page with links and latest sample   |
| `/wifi`  | Wi‑Fi credential portal (AP & STA modes) |
| `/json`  | Latest sample as JSON                    |
| `/uno`   | View UNO tunables                        |
| `/nano`  | View Nano tunables                       |
| `/stats` | Rolling statistics overview              |

---

## Accuracy & PPS Discipline

- The Nano captures **GPS PPS** edges into `ppsBuffer` and computes a **correction factor** mapping hardware ticks to real time.
- An **EMA filter** (`ppsEmaShift`) smooths the correction. `correctionJumpThresh` ignores outliers.
- Adjusted units (`adjusted_*`) have the correction applied; `raw_cycles` is uncorrected.

> Allow several PPS cycles for EMA to settle after power‑on.

### Timing Resolution & Quantization Error

With `F_CPU = 16 MHz` (Arduino Nano Every default):

- **Timer tick period:** 62.5 ns
- **Quantization (±1 tick per timestamp, two timestamps per swing):** ~125 ns
- **Equivalent error:**
  - 0.25 ppm over a 0.5 s half-period
  - 0.125 ppm over a full 1 s period

> Note: This is the digital timing floor. Real-world accuracy is dominated by sensor edge jitter (typically 1–5 µs), corresponding to ~2–10 ppm per half-period, which averages down as (1/\sqrt{N}) over multiple swings. GPS PPS disciplining removes long-term clock drift but does not reduce single-swing sensor jitter.

---

## Diagnostics & Troubleshooting

- `stats` (Nano) shows buffer fill, drops, PPS EMA.
- `/stats` (UNO) shows rolling metrics.
- Ring buffers drop oldest/newest entries if full.
- If sensor readings are NaN/0, check I²C wiring & libs.
- No logs? Ensure SD is FAT32, writable, and inserted before run.

---

## Build Notes

- Keep buffer sizes powers of two.
- If changing Nano pins/timers, update EVSYS & TCB setup.
- Use consistent capitalization: PPS, CSV, EEPROM, Wi‑Fi, UNO R4, Nano Every.
- Limitations of the Arduino IDE make it difficult to share single .h among multiple .ino sketches.  For this reason, there are a limited number of `gotchas` to be aware of
  - src/PendulumProtocol.h should remain identical between the Nano Every firmware (external) and `Uno.R4/` sources (manually copy as needed).
  - There will be some Config.h stuff too ...

---

## License

Choose a license (MIT/BSD/Apache-2.0) and place it as `LICENSE` at repo root.
