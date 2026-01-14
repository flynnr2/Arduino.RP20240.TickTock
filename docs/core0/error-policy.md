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

## SD failures
### SD missing at boot
- `sd_available=false`
- logging disabled
- `/status` shows “SD unavailable”

### SD removed mid-run / write failure
- stop logging (close file)
- set error counter + last error code
- continue running (ingest + UI + HTTP continue)
- optional periodic remount attempts (slow cadence)

---

## WiFi failures
### STA connection fails
- retry with backoff
- if AP fallback enabled: start AP after timeout

### WiFi drops during run
- reconnect with backoff
- HTTP server should degrade gracefully (may be unreachable, but device continues logging/UI)

---

## Sensor failures
- Missing device: mark unavailable and keep running.
- Read error: increment counter; keep last-good sample; mark invalid if stale.

---

## OLED failures
- If OLED init fails: disable UI and continue.
- Do not block on display writes; if write fails repeatedly, disable.

---

## Queue/ingest pressure
- If Core0 cannot keep up:
  - track it (counters, high-water mark if available)
  - reduce optional work:
    - lower OLED refresh rate
    - reduce HTTP stats work
    - reduce SD flush frequency (but keep bounded)
- Never try to “fix” by slowing Core1.

---

## Watchdogs and recovery (optional)
- If a subsystem is repeatedly failing, attempt a controlled reinit:
  - SD re-mount
  - WiFi reconnect
  - sensor re-begin
- A full reboot is a last resort; if used, log reboot reason and counter.

---

## Acceptance checks
- Pull SD during run: capture and ingest continue; `/status` reports SD error.
- Turn off AP/router: device continues; reconnect attempts do not starve ingest.
- Remove a sensor: device continues; env fields are marked missing/invalid.
