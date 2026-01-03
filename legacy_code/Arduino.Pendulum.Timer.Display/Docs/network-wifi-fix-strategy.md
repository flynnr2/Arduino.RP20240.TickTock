# Network, Access Point, and EEPROM Safety Strategy

This document outlines a concrete implementation plan to fix the Wi-Fi/AP startup robustness, HTTP credential handling, EEPROM layout overlap, and station retry behavior noted during the review.

## 1. Separate EEPROM regions for Wi-Fi credentials and tunables

* **Define non-overlapping slots.** Move Wi-Fi credential slots away from the shared (`EEPROM_UNO_SHARED_SLOT_*`) and Uno-specific (`EEPROM_UNO_SLOT_*`) tunable ranges defined in `Config.h` (currently occupying 0–255). Reserve a dedicated block (e.g., starting at byte 256) and update the Wi-Fi slot macros and `static_assert` bounds in `WiFiStorage.cpp` accordingly.
* **Consolidate size checks.** Add a single layout guard that asserts the end of the second Wi-Fi slot plus its record size does not exceed `EEPROM_SIZE` and does not overlap the tunable regions.
* **Migrate existing data safely.** When changing the addresses, attempt to read legacy slots at the old addresses first; if valid records are found, copy them into the new layout and clear the old `valid` flag to prevent future reads.

## 2. Validate access-point startup and reflect actual mode

* **Check `WiFi.beginAP` results.** Capture the return value and poll for `WL_AP_LISTENING` (with a timeout) before marking `apMode = true` or announcing AP availability on the display. On failure, log the error, keep `apMode` false, and consider retrying or falling back to a limited retry loop.
* **Defer AP banner logging until active.** Only call `Display::scrollLog` once the AP IP is available to avoid misleading “AP Mode” logs when the radio failed to start.

## 3. Harden HTTP credential submission and restart flow

* **Switch to POST.** Change the Wi-Fi config form in `HttpServer.cpp` to use `method='post'` and parse the body instead of query parameters, so passwords are not echoed in URLs or logs. Apply the same to the `/uno` handler’s Wi-Fi fields.
* **Delay reset until after response flush.** After writing the success page, give the client a short window (e.g., 1–2 seconds) to receive the response before invoking `NVIC_SystemReset()`, or provide a “Reconnect” button instead of immediate reset.
* **Input hygiene.** Add length checks before copying request fields into the credential buffers and zero sensitive buffers after use to reduce the chance of leakage.

## 4. Add station reconnection attempts while in AP mode

* **Background retry loop.** Track a retry timer in `loop()` and, when `apMode` is true, periodically attempt `WiFi.begin` with the stored credentials. If a station connection succeeds, stop the AP and restart the HTTP server in STA mode.
* **Expose status in UI.** Update the Wi-Fi status section (`/wifi`) to show the next retry time and current connection attempts so users know the device will eventually leave AP mode when the network returns.

## 5. Testing matrix

* **EEPROM layout regression.** Unit-test `WiFiStorage` read/write against the new addresses and verify tunable reads/writes still succeed. Where hardware tests are not possible, add a host-side layout-size static assertion and simulated EEPROM buffer tests.
* **AP/STA transitions.** Validate that failed AP creation leaves the device in a known state, and that STA retries bring the device back online without a reboot.
* **HTTP form handling.** Confirm POST submissions save credentials correctly, and that the reset/reconnect flow does not truncate responses.
