# Display layout refresh strategy (BPM precision & environment rows)

This note captures a proposed refinement of `Display::update()` for the 128×64 (21×8 text) OLED layout, incorporating 3-significant-digit BPM values and environmental readings.

## Goals
- Keep the display readable by standardizing top metrics and pushing status to the bottom bar.
- Show BPM and AVG BPM with 3 significant digits to reduce jitter while preserving precision.
- Add temperature, humidity, and pressure on rows 5–6 with terse labels.
- Preserve the inverse bottom status bar with fixed-width `LOG` and `GPS` fields, plus the IP line above it.

## Proposed layout (21 chars × 8 rows)
```
BPM: 123             
AVG BPM: 118         
PERIOD: 1987ms       
GPS CF: 1.003        
T:23.4C H:45%        
P:1013hPa            
IP: 192.168.0.42     
LOG: ON  GPS: LOCK   
```
- Rows 1–4: BPM, AVG BPM, period (tick+tock+blocks converted to GPS-adjusted ms), and GPS correction factor. Use `printf`-style width/precision (e.g., `display.printf("BPM: %6.3f", st.bpm);`) to lock each value to 3 significant digits.
- Row 5: Temperature and humidity sharing one line with compact labels (`T:` and `H:`). Values can use one decimal for temp and integer percent for humidity to stay within width.
- Row 6: Pressure in hPa with a brief `P:` label.
- Row 7: IP address (`WiFi.localIP()`), left-aligned.
- Row 8 (inverse): `fillRect` the final row and set inverted text color for `LOG: ON|OFF` and `GPS: LOCK|ACQR|NONE` with fixed-width fields (`LOG` 3 chars, `GPS` 4 chars) to keep spacing stable.

## Implementation hints
- Use the existing font height (`FONT_HEIGHT`) to set cursor positions per row; `setCursor(0, rowIndex * FONT_HEIGHT)` will map directly to the 8 text rows.
- Source data is already in the current sample and stats structures:
  - BPM/AVG BPM from `StatsEngine::get()` (`Display.cpp` uses `st` for these). 【F:Uno.R4/src/Display.cpp†L16-L57】
  - Period components and GPS correction (`tick`, `tock`, `tick_block`, `tock_block`, `corr_blend_ppm`) are in `NanoComm::currentSample` (`Display.cpp` copies this into `currentSample`). 【F:Uno.R4/src/Display.cpp†L37-L56】
  - Environmental values (`temperature_C`, `humidity_pct`, `pressure_hPa`) are part of the `PendulumSample` struct populated via the nano protocol. 【F:Uno.R4/src/PendulumProtocol.h†L90-L99】
- Consider `display.printf` for compact formatting to avoid manual padding; otherwise, preformat into fixed-width buffers with `snprintf` and print.
- Keep the splash/scroll functions unchanged; only reorder and format within `Display::update()` to match the mock-up.

