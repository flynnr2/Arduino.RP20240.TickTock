#pragma once
#include <WiFiS3.h>
#include "Config.h"
#include "StatsEngine.h"
#include "WiFiConfig.h"
#include "NanoComm.h"

namespace HttpServer {
  void begin();
  void service();
}
