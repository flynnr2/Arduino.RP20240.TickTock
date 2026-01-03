#include "secrets.h"
#include "WiFiConfig.h"
#include "WiFiStorage.h"
#include "Display.h"
#include <cstring>
#include <Arduino.h>

namespace WiFiConfig {

static char g_ssid[MAX_SSID_LEN] = {0};
static char g_pass[MAX_PASS_LEN] = {0};
static char g_apPass[MAX_PASS_LEN] = {0};
static bool apMode = false;
static bool pendingReconnect = false;
static bool provisioning = false;
static unsigned long lastReconnectAttemptMs = 0;
static unsigned long lastApStartMs = 0;
static uint8_t apDropStrikes = 0;
static unsigned long apDropFirstMs = 0;

static void ensureApPassword();
static bool startAccessPoint();
static bool connectToWiFi(const char* s, const char* p);

const char* ssid() { return g_ssid; }
const char* apPassword() { return g_apPass[0] ? g_apPass : AP_PASS; }
bool isApMode() { return apMode; }
bool isProvisioning() { return provisioning; }
void setProvisioning(bool enable) {
  if (enable == provisioning) return;
  provisioning = enable;
  if (provisioning) {
    Display::scrollLog(F("Provisioning enabled"));
    startAccessPoint();
  } else {
    Display::scrollLog(F("Provisioning disabled; reconnecting"));
    pendingReconnect = true;
    lastReconnectAttemptMs = 0;
  }
}
void requestReconnect() {
  pendingReconnect = true;
  lastReconnectAttemptMs = 0;
}
void setCredentials(const char* newSsid, const char* newPass, bool connectNow) {
  if (newSsid) {
    strncpy(g_ssid, newSsid, sizeof(g_ssid));
    g_ssid[sizeof(g_ssid) - 1] = 0;
  }
  if (newPass) {
    strncpy(g_pass, newPass, sizeof(g_pass));
    g_pass[sizeof(g_pass) - 1] = 0;
  }
  if (connectNow && provisioning) {
    provisioning = false;
  }
  if (connectNow || !provisioning) {
    pendingReconnect = true;
    lastReconnectAttemptMs = 0;
  }
}

static void ensureApPassword() {
  if (g_apPass[0]) return;
  if (std::strlen(AP_PASS) > 0) {
    strncpy(g_apPass, AP_PASS, sizeof(g_apPass));
    g_apPass[sizeof(g_apPass) - 1] = 0;
    return;
  }
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  randomSeed((uint32_t)micros() ^ (uint32_t)millis());
  const size_t passLen = 12;
  for (size_t i = 0; i < passLen && i < sizeof(g_apPass) - 1; i++) {
    g_apPass[i] = alphabet[random(sizeof(alphabet) - 1)];
  }
  g_apPass[passLen] = 0;
}

static bool startAccessPoint() {
  unsigned long now = millis();
  if (lastApStartMs != 0 && now - lastApStartMs < WIFI_AP_RESTART_BACKOFF_MS) {
    return apMode;
  }
  lastApStartMs = now;
  WiFi.end();
  delay(WIFI_AP_END_DELAY_MS);   // give radio time to shut down before switching modes
  ensureApPassword();
  WiFi.beginAP(AP_SSID, g_apPass);
  unsigned long start = millis();
  constexpr uint8_t requiredStablePolls = 3;
  uint8_t stablePolls = 0;
  while (millis() - start < WIFI_AP_START_TIMEOUT_MS) {
    int status = WiFi.status();
    if (status == WL_AP_LISTENING || status == WL_AP_CONNECTED) {
      apMode = true;
      IPAddress ip = WiFi.localIP();
      Display::scrollLog(String("AP Mode: ") + AP_SSID);
      Display::scrollLog(String("AP Pass: ") + g_apPass);
      Display::scrollLog(String("IP: ") + ip.toString());
      return true;
    }
    delay(WIFI_AP_START_DELAY_MS);
  }
  apMode = false;
  Display::scrollLog(F("AP start failed"));
  return false;
}

static bool connectToWiFi(const char* s, const char* p) {
  if (!s || strlen(s) == 0) return false;
  WiFi.end();
  delay(WIFI_AP_END_DELAY_MS);
  WiFi.begin(s, p);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(WIFI_CONNECT_RETRY_MS); // retry connection check periodically
  }
  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    Display::scrollLog(String("WiFi OK: ") + WiFi.localIP().toString());
    return true;
  }
  Display::scrollLog(F("WiFi connect failed"));
  return false;
}

void begin() {
  ensureApPassword();
  WiFiStorage::readCredentials(g_ssid, g_pass);
//XXX need to sort out AP
//  if (g_ssid[0] == '\0' && WIFI_SSID[0] != '\0') {
  if (WIFI_SSID[0] != '\0') {
    strncpy(g_ssid, WIFI_SSID, sizeof(g_ssid));
    g_ssid[sizeof(g_ssid)-1] = 0;
    strncpy(g_pass, WIFI_PASS, sizeof(g_pass));
    g_pass[sizeof(g_pass)-1] = 0;
  }
  if (provisioning) {
    startAccessPoint();
    return;
  }
  if (!connectToWiFi(g_ssid, g_pass)) {
    startAccessPoint();
  }
}

void service() {
  unsigned long now = millis();
  int status = WiFi.status();
  bool haveCreds = g_ssid[0] != '\0';

  if (provisioning) {
    if (!apMode || (status != WL_AP_LISTENING && status != WL_AP_CONNECTED)) {
      startAccessPoint();
    }
    return;
  }

  if (!apMode) {
    if (status != WL_CONNECTED && haveCreds && now - lastReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      lastReconnectAttemptMs = now;
      if (!connectToWiFi(g_ssid, g_pass)) {
        startAccessPoint();
      }
    }
  } else {
    if (status != WL_AP_LISTENING && status != WL_AP_CONNECTED) {
      if (apDropFirstMs == 0) {
        apDropFirstMs = now;
      }
      if (apDropStrikes < UINT8_MAX) {
        apDropStrikes++;
      }

      const bool graceExpired = WIFI_AP_DROP_GRACE_MS > 0 && now - apDropFirstMs >= WIFI_AP_DROP_GRACE_MS;
      if (apDropStrikes >= 3 || graceExpired) {
        Display::scrollLog(F("AP restarting"));
        apDropStrikes = 0;
        apDropFirstMs = 0;
        startAccessPoint();
      }
    } else {
      apDropStrikes = 0;
      apDropFirstMs = 0;
    }
  }

  if (pendingReconnect && haveCreds && now - lastReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
    lastReconnectAttemptMs = now;
    if (connectToWiFi(g_ssid, g_pass)) {
      pendingReconnect = false;
    }
  }
}

} // namespace WiFiConfig
