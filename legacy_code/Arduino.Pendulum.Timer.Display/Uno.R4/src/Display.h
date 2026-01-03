#pragma once

#include <Adafruit_SSD1306.h>

#include "Config.h"
#include "NanoComm.h"
#include "PendulumProtocol.h"
#include "StatsEngine.h"

namespace Display {
  void begin();
  void showSplash();
  void scrollLog(const char* msg);
  void scrollLog(const String &msg);
  void scrollLog(const __FlashStringHelper* fmsg);
  void update();
}
