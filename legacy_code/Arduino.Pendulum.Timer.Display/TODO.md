# TODO for Pendulum Timer Display/Logger (UNO R4 WiFi)

This file summarizes the key code review findings and recommended changes.

---

## High Impact

- **Fix `/json` endpoint**: Currently returns CSV but sets `Content-Type: application/json`.  
  - Option A: Return proper JSON.  
  - Option B: Rename to `/sample.csv` and set `Content-Type: text/csv`.

- **Prevent torn reads of `currentSample`**:  
  - Snapshot `NanoComm::currentSample` before use (HTTP, Display).  
  - Use `memcpy` or guard with interrupts.

- **Secure AP mode**:  
  - Current AP is open. Switch to WPA2 with a randomly generated password on first boot.  
  - Display SSID/pass on OLED and `/wifi`.

- **Increase CSV/header/Nano line buffer sizes**:  
  - `NanoComm::csvHeader`, `SDLogger::csvBuf`, `NANO_LINE_MAX`.  
  - Recommend 192–256 bytes to avoid truncation.  
  - SRAM cost negligible.

---

## Nice to Have

- **Non-blocking serial line reads**: Replace `readBytesUntil` with manual line buffer assembly using `available()`.  
  - Reduces 50 ms stalls if newline missing.

- **Reduce `String` usage on hot paths**:  
  - Replace with `snprintf` + `char[]` for Wi-Fi status, I²C scan, display logs.  
  - Prevents heap fragmentation.

- **Stats window capacity**:  
  - Currently capped at compile-time `DEFAULT_STATS_WINDOW`.  
  - Increase cap (e.g., 512/1024) or dynamically allocate.  
  - Tradeoff: Static RAM vs. flexibility.

- **Explicit I²C init**:  
  - Add `Wire.begin(); Wire.setClock(400000);` in `Sensors::begin()`.  
  - Allow BMP280 address override.

---

## Later / Optional

- **POST for `/wifi` config**: Avoids exposing password in browser history/URLs.  
  - Keep GET for convenience.

- **Add `Content-Length` in HTTP responses**: Improves client compatibility and closes connections deterministically.

- **Optional watchdog**: For unattended recovery from hangs.

---

## Quick Patchlets

**Snapshot read to avoid torn samples**
```cpp
PendulumSample s;
memcpy(&s, &NanoComm::currentSample, sizeof s);
client.print(F("tick: "));
client.println(NanoComm::ticksToMicros(s.tick));
```

**Avoid `String` on IP display**
```cpp
char ipbuf[24];
IPAddress ip = WiFi.localIP();
snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
Display::scrollLog(ipbuf);
```

**Bigger CSV buffers**
```cpp
constexpr size_t CSV_BUF_MAX = 256;
```

**Secure AP (if supported by WiFiS3)**
```cpp
char pass[16]; makeRandomPass(pass, sizeof(pass));
WiFi.beginAP(AP_SSID, pass);
// Show on OLED + /wifi page
```

---

## Testing Checklist

1. **Serial parser fuzz**: Test valid/invalid lines, header negotiation.  
2. **AP fallback**: Boot no creds → AP → add creds → STA → break creds → AP again.  
3. **Power cut**: Verify CSV integrity (`TRUNCATED_LINE` as needed).  
4. **HTTP soak**: Hit `/` and `/uno` rapidly while streaming; check responsiveness & memory stability.

---
