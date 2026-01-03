#include "SDLogger.h"
#include "Display.h"
#include "NanoComm.h"
#include "WiFiConfig.h"
#include <WiFiS3.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace SDLogger {

static File logFile;
static bool sdReady = false;
static bool loggingEnabled = false;
static bool appendMode = LOG_APPEND_DEFAULT;
static LogMode logMode = LogMode::Continuous;
static char baseFile[LOG_FILENAME_LEN] = LOG_FILENAME;
static char currentFile[LOG_FILENAME_LEN] = LOG_FILENAME;
static uint16_t writesSinceFlush = 0;
static unsigned long lastFlushMs = 0;

static time_t ntpEpoch = 0;
static unsigned long ntpSyncMs = 0;
static unsigned long lastNtpAttemptMs = 0;
static const unsigned long NTP_RETRY_MS   = 15000;
static const unsigned long NTP_RESYNC_MS  = 3600000UL;
static int32_t activeDayIndex = -1;
static uint32_t fallbackDayIndex = 0;
static unsigned long fallbackDayStartMs = 0;

static bool timeIsValid(time_t t) { return t > 1600000000; }

static bool validFilename(const char* fn) {
  if (!fn || !*fn) return false;
  size_t len = strlen(fn);
  if (len >= LOG_FILENAME_LEN) return false;
  for (size_t i=0;i<len;i++) {
    char c = fn[i];
    if (!(isalnum(c) || c=='_' || c=='-' || c=='.')) return false;
  }
  return true;
}

bool isValidFilename(const char* fn) { return validFilename(fn); }
bool ready() { return sdReady && logFile; }
bool isLogging() { return loggingEnabled && logFile; }

static void resetFallbackClock() {
  fallbackDayStartMs = millis();
  fallbackDayIndex = 0;
}

static void attemptNtpSync() {
  unsigned long now = millis();
  if (now - lastNtpAttemptMs < NTP_RETRY_MS) return;
  lastNtpAttemptMs = now;
  if (WiFi.status() != WL_CONNECTED) return;

  for (uint8_t tries = 0; tries < 3; ++tries) {
    time_t candidate = WiFi.getTime();
    if (timeIsValid(candidate)) {
      ntpEpoch = candidate;
      ntpSyncMs = millis();
      fallbackDayIndex = (uint32_t)(candidate / 86400UL);
      fallbackDayStartMs = millis() - (unsigned long)((candidate % 86400UL) * 1000UL);
      Display::scrollLog(F("NTP sync ok"));
      return;
    }
    delay(50);
  }
  if (!timeIsValid(ntpEpoch)) {
    Display::scrollLog(F("NTP retry"));
  }
}

bool hasTimeSync() { return timeIsValid(ntpEpoch); }

time_t currentEpoch() {
  if (!hasTimeSync()) return 0;
  unsigned long elapsed = (millis() - ntpSyncMs) / 1000UL;
  return ntpEpoch + (time_t)elapsed;
}

unsigned long secondsSinceLastSync() {
  if (!hasTimeSync()) return ULONG_MAX;
  return (millis() - ntpSyncMs) / 1000UL;
}

static int32_t computeDayIndex(time_t epochNow) {
  if (timeIsValid(epochNow)) {
    return (int32_t)(epochNow / 86400UL);
  }
  unsigned long nowMs = millis();
  if (fallbackDayStartMs == 0) resetFallbackClock();
  while (nowMs - fallbackDayStartMs >= 86400000UL) {
    fallbackDayStartMs += 86400000UL;
    ++fallbackDayIndex;
  }
  return (int32_t)fallbackDayIndex;
}

static void buildDailyFilename(char* out, size_t len, time_t epochNow, int32_t dayIndex) {
  if (timeIsValid(epochNow)) {
    struct tm* tmNow = gmtime(&epochNow);
    if (tmNow) {
      snprintf(out, len, "%04d%02d%02d.csv", tmNow->tm_year + 1900, tmNow->tm_mon + 1, tmNow->tm_mday);
      return;
    }
  }
  snprintf(out, len, "day%04ld.csv", (long)(dayIndex % 10000L));
}

static bool openLogFile(const char* fname, bool appendFlag) {
  if (fname && validFilename(fname)) {
    strncpy(currentFile, fname, LOG_FILENAME_LEN - 1);
    currentFile[LOG_FILENAME_LEN - 1] = 0;
  }
  if (!sdReady) {
    loggingEnabled = false;
    if (logFile) { logFile.close(); }
    logFile = File();
    Display::scrollLog(F("SD not ready"));
    return false;
  }
  if (logFile) { logFile.close(); }
  if (!appendFlag) SD.remove(currentFile);
  logFile = SD.open(currentFile, FILE_WRITE);
  if (!logFile) {
    Display::scrollLog(F("open fail"));
    loggingEnabled = false;
    logFile = File();
    return false;
  }
  loggingEnabled = true;
  if (!appendFlag || logFile.size() == 0) {
    logFile.println(NanoComm::getCSVHeader());
    logFile.flush();
  }
  writesSinceFlush = 0;
  lastFlushMs = millis();
  return true;
}

void begin() {
  if (logFile) { logFile.close(); }
  if (!SD.begin(SD_CS_PIN)) {
    Display::scrollLog(F("SD init failed"));
    sdReady = false;
    loggingEnabled = false;
    logFile = File();
    return;
  }
  sdReady = true;
  loggingEnabled = false;
  appendMode = LOG_APPEND_DEFAULT;
  strncpy(baseFile, LOG_FILENAME, LOG_FILENAME_LEN - 1);
  baseFile[LOG_FILENAME_LEN - 1] = 0;
  strncpy(currentFile, baseFile, LOG_FILENAME_LEN - 1);
  currentFile[LOG_FILENAME_LEN - 1] = 0;
  resetFallbackClock();
  attemptNtpSync();
  if (logFile) { logFile.close(); }
}

void setFilename(const char *fname) {
  if (validFilename(fname)) {
    strncpy(baseFile, fname, LOG_FILENAME_LEN - 1);
    baseFile[LOG_FILENAME_LEN - 1] = 0;
  }
}

void setAppendMode(bool append) { appendMode = append; }
void setLogMode(LogMode mode) { logMode = mode; }

const char* getFilename() { return baseFile; }
const char* getActiveFilename() { return currentFile; }
bool getAppendMode() { return appendMode; }
LogMode getLogMode() { return logMode; }

bool startLogging(const char* fname, bool append) {
  setLogMode(LogMode::Continuous);
  setFilename(fname);
  setAppendMode(append);
  activeDayIndex = -1;
  return openLogFile(baseFile, appendMode);
}

bool startLogging(LogMode mode, bool forceNewFile) {
  logMode = mode;
  char target[LOG_FILENAME_LEN];
  bool appendFlag = appendMode;

  if (logMode == LogMode::Daily) {
    time_t epochNow = currentEpoch();
    int32_t dayIndex = computeDayIndex(epochNow);
    buildDailyFilename(target, sizeof(target), epochNow, dayIndex);
    bool newDay = (activeDayIndex >= 0 && dayIndex != activeDayIndex);
    if (forceNewFile || newDay) {
      appendFlag = false;
    }
    activeDayIndex = dayIndex;
  } else {
    strncpy(target, baseFile, sizeof(target) - 1);
    target[sizeof(target) - 1] = 0;
    if (forceNewFile) appendFlag = false;
    activeDayIndex = -1;
  }

  return openLogFile(target, appendFlag);
}

bool restartLogging(bool forceNewFile) {
  stopLogging();
  return startLogging(logMode, forceNewFile);
}

void stopLogging() {
  if (logFile) {
    logFile.flush();
    logFile.close();
  }
  loggingEnabled = false;
}

void writeHeader(const char *hdr) {
  if (!ready()) return;
  logFile.println(hdr);
  logFile.flush();
}

static void checkRollover() {
  if (!isLogging()) return;
  if (logMode != LogMode::Daily) return;
  time_t epochNow = currentEpoch();
  int32_t dayIndex = computeDayIndex(epochNow);
  if (activeDayIndex < 0) {
    activeDayIndex = dayIndex;
    return;
  }
  if (dayIndex != activeDayIndex) {
    restartLogging(true);
  }
}

void logSample(const PendulumSample &s) {
  if (!isLogging()) return;
  checkRollover();
  if (!isLogging()) return;

  static char csvBuf[256];
  int len = snprintf(csvBuf, sizeof(csvBuf),
    "%lu,%lu,%lu,%lu,%ld,%ld,%u,%u,%.2f,%.2f,%.2f\n",
    (unsigned long)s.tick,
    (unsigned long)s.tock,
    (unsigned long)s.tick_block,
    (unsigned long)s.tock_block,

    (long)s.corr_inst_ppm,
    (long)s.corr_blend_ppm,
    (unsigned int)s.gps_status,
    (unsigned int)s.dropped_events,
    s.temperature_C,
    s.humidity_pct,
    s.pressure_hPa);
  if (len > 0 && len < (int)sizeof(csvBuf)) {
    logFile.write((const uint8_t*)csvBuf, len);
  } else {
    logFile.println(F("TRUNCATED_LINE"));
  }
  writesSinceFlush++;
  unsigned long now = millis();
  bool doFlush = (writesSinceFlush >= FLUSH_EVERY_N) || ((now - lastFlushMs) >= FLUSH_EVERY_MS);
  if (doFlush) {
    logFile.flush();
    writesSinceFlush = 0;
    lastFlushMs = now;
  }
}

void service() {
  unsigned long now = millis();
  if (!hasTimeSync() || (now - ntpSyncMs) > NTP_RESYNC_MS) {
    attemptNtpSync();
  }
  if (loggingEnabled && logMode == LogMode::Daily) {
    checkRollover();
  }
}

} // namespace SDLogger
