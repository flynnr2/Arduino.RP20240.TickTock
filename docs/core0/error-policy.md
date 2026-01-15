# Core0 Error Policy

This document defines how the system behaves when subsystems fail. The guiding rule is:

> **Capture (Core1) must never depend on SD/WiFi/OLED/sensors.**  
> Core0 must degrade *presentation and persistence*, not ingest/capture.

---

## General rules
- The ingest drain runs first and must remain fast.
- Any long operation must be time-sliced (chunked) with yields.
- Report truthfully: `/status` and OLED must reflect failures.

---

## Stable-first scale (avoid jitter during ACQUIRING)
- Core0 may compute `pps_cycles_last_good` and derived seconds for UI/stats.
- Update the scale **only** when `gps_state==LOCKED` and PPS passes quality gates.
- In `ACQUIRING`/`BAD_JITTER`, hold last-good scale; do not chase.

Raw logging (cycles) remains authoritative regardless of scale validity.

---

## SD failures (raw + stats logs)
### SD missing at boot
- `sd_available=false`
- raw/stats logging disabled
- `/status` shows “SD unavailable”

### SD removed mid-run / write failure
- stop logging (close raw + stats files)
- set `FLAG_SD_ERROR` + increment SD error counters
- continue running (ingest + UI + HTTP continue)
- optional periodic remount attempts (slow cadence)

---

## WiFi failures
### STA connection fails
- retry with backoff
- if AP fallback enabled: start AP after timeout

### WiFi drops during run
- reconnect with backoff
- HTTP server should degrade gracefully (device continues logging/UI)
- set `FLAG_WIFI_DOWN` while disconnected

---

## Sensor failures
- Missing device: mark unavailable and keep running (`FLAG_SENSOR_MISSING`).
- Read error: increment counter; keep last-good sample; mark invalid if stale.

---

## OLED failures
- If OLED init fails: disable UI and continue (`FLAG_OLED_ERROR`).
- Do not block on display writes; if write fails repeatedly, disable.

---

## Queue/ingest pressure
- If Core0 cannot keep up:
  - track it (queue health counters)
  - reduce optional work:
    - lower OLED refresh rate
    - reduce stats compute work and/or stats CSV cadence
    - reduce SD flush frequency (but keep bounded)
- Never “fix” by slowing Core1.

---

## Watchdogs and recovery (optional)
- If a subsystem is repeatedly failing, attempt a controlled reinit:
  - SD re-mount
  - WiFi reconnect
  - sensor re-begin
- A full reboot is a last resort; if used, log reboot reason and counter.

---

## Acceptance checks
- Pull SD during run: capture and ingest continue; `/status` reports SD error; OLED shows SD ERR.
- Turn off AP/router: device continues; reconnect attempts do not starve ingest.
- Remove a sensor: device continues; env fields marked missing/invalid.


---

## Degraded mode (backpressure)
If queue pressure rises or `FLAG_RING_OVERFLOW` occurs:
- record it (counters)
- reduce optional work in priority order:
  1. lower OLED refresh rate
  2. reduce stats compute work/window sizes
  3. reduce stats CSV cadence
  4. reduce SD flush frequency (keep bounded)
- never slow Core1 capture
