#pragma once
#include <WiFiS3.h>
#include "Config.h"

namespace WiFiConfig {
  void begin();
  void service();
  const char* ssid();
  const char* apPassword();
  bool isApMode();
  bool isProvisioning();
  void setProvisioning(bool enable);
  void requestReconnect();
  void setCredentials(const char* newSsid, const char* newPass, bool connectNow = false);
}
