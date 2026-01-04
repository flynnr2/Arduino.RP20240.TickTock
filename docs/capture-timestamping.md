# Capture timing on RP2040 (arduino-pico — **chosen**): options and recommendation


## Decision (locked)
- Arduino core: **arduino-pico**
- Capture: **PIO edge capture + DMA → RAM ring** from the start
- Core1 drains/processes the RAM ring and outputs per-swing samples
- Core0 enriches/logs/serves UI; no capture responsibilities


## Why DMA from the start
- FIFO-only designs drop edges when CPU draining falls behind; DMA drains FIFO automatically.
- Best robustness under combined WiFi/HTTP/SD load.
- Better supports faster pendulums without redesign.
