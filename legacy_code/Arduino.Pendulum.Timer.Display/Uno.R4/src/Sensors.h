#pragma once
#include <Adafruit_BMP280.h>
#include <Adafruit_SHT4x.h>
#include "Config.h"

namespace Sensors {
  void begin();
  void poll();
  void getLatest(float &temperature, float &humidity, float &pressure);
  void scanI2C();
}
