#pragma once

#include "Common.h"
#include "PendulumProtocol.h"

namespace NanoComm {
  extern PendulumSample currentSample;
  bool parseLine(const char* line);
  void readStartup();
  bool streamingStarted();
  const char* getCSVHeader();
  uint32_t ticksToMicros(uint32_t ticks);
  float ticksToMs(uint32_t ticks, int32_t corr_ppm);
  uint32_t ticksToUnits(uint32_t ticks, int32_t corr_blend_ppm);
  DataUnits getDataUnits();
}
