#pragma once

#include "Config.h"
#include "NanoComm.h"
#include "PendulumProtocol.h"

struct RollingSample {
  uint32_t tick;        // raw ticks
  uint32_t tock;        // raw ticks
  uint32_t tick_block;  // raw ticks
  uint32_t tock_block;  // raw ticks
  uint32_t timestamp_ms;
  int32_t  corr_blend_ppm; // blended correction for this sample
  uint32_t block_jump_mag;
  bool     has_prev_block;
};

struct RollingStats {
  float bpm;
  float delta_beat;
  float delta_block;
  float avg_bpm;
  float avg_delta_beat;
  float avg_delta_block;
  float avg_block_jump;
  float avg_period_us;
  float stddev_bpm;
  float stddev_delta_beat;
  float stddev_delta_block;
  float stddev_block_jump;
  float stddev_period_us;
};

namespace StatsEngine {
  void reset();
  void update();
  const RollingStats &get();
  uint16_t windowCount();
  uint16_t windowCapacityLimit();
}
