#pragma once
#include <SD.h>
#include <time.h>
#include "Config.h"
#include "PendulumProtocol.h"

namespace SDLogger {
  enum class LogMode : uint8_t { Continuous = 0, Daily = 1 };

  void begin();
  void service();

  bool startLogging(const char* fname, bool append);
  bool startLogging(LogMode mode, bool forceNewFile = false);
  bool restartLogging(bool forceNewFile);
  void stopLogging();

  void setFilename(const char* fname);
  void setAppendMode(bool append);
  void setLogMode(LogMode mode);

  const char* getFilename();           // configured base filename (continuous)
  const char* getActiveFilename();     // currently open file path
  bool getAppendMode();
  LogMode getLogMode();

  bool isLogging();
  bool ready();

  bool hasTimeSync();
  time_t currentEpoch();
  unsigned long secondsSinceLastSync();

  bool isValidFilename(const char* fn);

  void writeHeader(const char *hdr);
  void logSample(const PendulumSample &s);
}
