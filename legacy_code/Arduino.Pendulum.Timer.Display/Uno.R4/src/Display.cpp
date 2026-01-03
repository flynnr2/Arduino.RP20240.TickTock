#include "Display.h"
#include "SDLogger.h"
#include <WiFiS3.h>
#include <cstdio>
#include <cstring>

namespace Display {

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

constexpr size_t SCROLL_LINES = 8;
constexpr size_t SCROLL_LINE_LEN = 64;
constexpr uint8_t FONT_HEIGHT = 8;
constexpr uint8_t TICKER_ROW = 7;
constexpr uint8_t TICKER_CHAR_WIDTH = 6; // default 5x7 font + 1px spacing
constexpr uint8_t TICKER_VISIBLE_CHARS = SCREEN_WIDTH / TICKER_CHAR_WIDTH;
constexpr uint32_t TICKER_SCROLL_INTERVAL_MS = 175;
constexpr uint32_t TICKER_PAUSE_MS = 1200;
constexpr uint32_t TICKER_ALERT_TTL_MS = 12000;
constexpr size_t MAX_TICKER_ALERTS = 6;
constexpr size_t MAX_TICKER_MESSAGES = MAX_TICKER_ALERTS + 4; // alerts + LOG/GPS/IP heartbeat

static char scrollBuffer[SCROLL_LINES][SCROLL_LINE_LEN];
static int scrollLineCount = 0;
static unsigned long tickerHoldUntilMs = 0;
static unsigned long tickerNextStepMs = 0;
static size_t tickerMessageIndex = 0;
static size_t tickerCharOffset = 0;

struct TickerAlert {
  String text;
  unsigned long expiresAtMs = 0;
  bool active = false;
};

static TickerAlert tickerAlerts[MAX_TICKER_ALERTS];
static size_t nextAlertSlot = 0;

static String tickerMessages[MAX_TICKER_MESSAGES];
static size_t tickerMessageCount = 0;

const RollingStats &st = StatsEngine::get();

static uint8_t min_u8(uint8_t a, uint8_t b) {
  return a < b ? a : b;
}

static bool hasValidIp(const IPAddress &ip) {
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

static void enqueueTickerAlert(const String &msg, unsigned long ttlMs) {
  tickerAlerts[nextAlertSlot].text = msg;
  tickerAlerts[nextAlertSlot].expiresAtMs = ttlMs ? millis() + ttlMs : 0;
  tickerAlerts[nextAlertSlot].active = true;
  nextAlertSlot = (nextAlertSlot + 1) % MAX_TICKER_ALERTS;
  tickerHoldUntilMs = 0;
  tickerNextStepMs = 0;
}

static void purgeExpiredAlerts(unsigned long now) {
  for (auto &alert : tickerAlerts) {
    if (alert.active && alert.expiresAtMs && static_cast<long>(now - alert.expiresAtMs) >= 0) {
      alert.active = false;
    }
  }
}

static void rebuildTickerMessages() {
  const unsigned long now = millis();
  purgeExpiredAlerts(now);

  size_t count = 0;
  for (const auto &alert : tickerAlerts) {
    if (alert.active && count < MAX_TICKER_MESSAGES) {
      tickerMessages[count++] = alert.text;
    }
  }

  if (count < MAX_TICKER_MESSAGES) {
    tickerMessages[count++] = String(F("LOG: ")) + (SDLogger::isLogging() ? F("ON") : F("OFF"));
  }

  if (count < MAX_TICKER_MESSAGES) {
    const __FlashStringHelper* gpsStatus = F("NONE");
    switch (NanoComm::currentSample.gps_status) {
      case GpsStatus::LOCKED:     gpsStatus = F("LOCK"); break;
      case GpsStatus::ACQUIRING:  gpsStatus = F("ACQR"); break;
      default:                    gpsStatus = F("NONE"); break;
    }
    tickerMessages[count++] = String(F("GPS: ")) + gpsStatus;
  }

  if (count < MAX_TICKER_MESSAGES) {
    IPAddress ip = WiFi.localIP();
    if (hasValidIp(ip)) {
      tickerMessages[count++] = String(F("IP: ")) + ip.toString();
    }
  }

  if (count == 0) {
    tickerMessages[count++] = F("STATUS: OK");
  }

  if (tickerMessageIndex >= count) {
    tickerMessageIndex = 0;
  }
  tickerMessageCount = count;
}

static void advanceTicker(const String &msg, unsigned long now) {
  const bool scrolling = msg.length() > TICKER_VISIBLE_CHARS;

  if (tickerHoldUntilMs == 0) {
    tickerHoldUntilMs = now + TICKER_PAUSE_MS;
    tickerNextStepMs = now + TICKER_SCROLL_INTERVAL_MS;
  }

  if (static_cast<long>(now - tickerHoldUntilMs) < 0) {
    return;
  }

  if (scrolling) {
    if (static_cast<long>(now - tickerNextStepMs) >= 0) {
      if (tickerCharOffset + TICKER_VISIBLE_CHARS < msg.length()) {
        tickerCharOffset++;
        tickerNextStepMs = now + TICKER_SCROLL_INTERVAL_MS;
        if (tickerCharOffset + TICKER_VISIBLE_CHARS >= msg.length()) {
          tickerHoldUntilMs = now + TICKER_PAUSE_MS;
        }
      } else {
        tickerCharOffset = 0;
        tickerMessageIndex = (tickerMessageIndex + 1) % tickerMessageCount;
        tickerHoldUntilMs = now + TICKER_PAUSE_MS;
        tickerNextStepMs = tickerHoldUntilMs;
      }
    }
  } else {
    tickerCharOffset = 0;
    tickerMessageIndex = (tickerMessageIndex + 1) % tickerMessageCount;
    tickerHoldUntilMs = now + TICKER_PAUSE_MS;
    tickerNextStepMs = tickerHoldUntilMs;
  }
}

static void renderTicker(const String &msg) {
  display.fillRect(0, TICKER_ROW * FONT_HEIGHT, SCREEN_WIDTH, FONT_HEIGHT, SSD1306_WHITE);
  display.setCursor(0, TICKER_ROW * FONT_HEIGHT);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);

  const uint8_t visible = min_u8(TICKER_VISIBLE_CHARS, msg.length());
  const uint8_t start = min_u8(tickerCharOffset, msg.length());
  const uint8_t end = min_u8(start + visible, msg.length());
  display.print(msg.substring(start, end));

  display.setTextColor(SSD1306_WHITE);
}

void begin() {
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void showSplash() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(F("Pendulum Monitor"));
  display.display();
}

void scrollLog(const char* m) {
  if (scrollLineCount >= SCROLL_LINES) {
    for (int i=0;i<SCROLL_LINES-1;i++) {
      strncpy(scrollBuffer[i], scrollBuffer[i+1], sizeof(scrollBuffer[0])-1);
      scrollBuffer[i][sizeof(scrollBuffer[0])-1] = 0;
    }
    strncpy(scrollBuffer[SCROLL_LINES-1], m, sizeof(scrollBuffer[0])-1);
    scrollBuffer[SCROLL_LINES-1][sizeof(scrollBuffer[0])-1] = 0;
  } else {
    strncpy(scrollBuffer[scrollLineCount], m, sizeof(scrollBuffer[0])-1);
    scrollBuffer[scrollLineCount][sizeof(scrollBuffer[0])-1] = 0;
    scrollLineCount++;
  }
  enqueueTickerAlert(String(m), TICKER_ALERT_TTL_MS);
}

void scrollLog(const String &msg) {
  scrollLog(msg.c_str());
}

void scrollLog(const __FlashStringHelper* fmsg) {
  scrollLog(String(fmsg).c_str());
}

void update() {
  display.clearDisplay();
  display.setTextSize(1);
  PendulumSample currentSample;
  memcpy(&currentSample, &NanoComm::currentSample, sizeof(currentSample));
  char line[22] = {0};

  auto setRow = [](uint8_t row) { display.setCursor(0, row * FONT_HEIGHT); };

  setRow(0);
  snprintf(line, sizeof(line), "BPM:     %9.5f", st.bpm);
  display.print(line);

  setRow(1);
  snprintf(line, sizeof(line), "AVG BPM: %9.5f", st.avg_bpm);
  display.print(line);

  uint32_t period_ticks = currentSample.tick + currentSample.tick_block +
                          currentSample.tock + currentSample.tock_block;
  float period_ms = NanoComm::ticksToMs(period_ticks,
                                        currentSample.corr_blend_ppm);

  setRow(2);
  snprintf(line, sizeof(line), "PERIOD:  %7.1fms", period_ms);
  display.print(line);

  float gps_cf = 1.0f +
                 (static_cast<float>(currentSample.corr_blend_ppm) /
                  static_cast<float>(CORR_PPM_SCALE));
  setRow(3);
  snprintf(line, sizeof(line), "GPS CF:    %7.3f", gps_cf);
  display.print(line);

  setRow(4);
  snprintf(line, sizeof(line), "T:%.2fC H:%04.2f%%", currentSample.temperature_C,
           currentSample.humidity_pct);
  display.print(line);

  setRow(5);
  snprintf(line, sizeof(line), "P:%.2fhPa", currentSample.pressure_hPa);
  display.print(line);

  rebuildTickerMessages();

  if (tickerMessageCount) {
    const String &currentMsg = tickerMessages[tickerMessageIndex];
    renderTicker(currentMsg);
    advanceTicker(currentMsg, millis());
  }

  display.display();
}

} // namespace Display
