#include "StatsEngine.h"
#include "Display.h"

#ifdef abs
#undef abs
#endif

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace StatsEngine {

static constexpr float NANO_TICK_FREQ_F = 16000000.0f;

static float stddev(double sum, double sumSq, uint16_t count);

static float unitsPerMinute(int32_t corr_blend_ppm) {
  switch (NanoComm::getDataUnits()) {
    case DataUnits::RawCycles: {
      double adjustedFreq = (double)NANO_TICK_FREQ_F * (double)(CORR_PPM_SCALE + corr_blend_ppm) / (double)CORR_PPM_SCALE;
      return (float)(adjustedFreq * 60.0);
    }
    case DataUnits::AdjustedNs: return 60000000000.0f;
    case DataUnits::AdjustedMs: return 60000.0f;
    case DataUnits::AdjustedUs: default: return 60000000.0f;
  }
}

static constexpr uint16_t MAX_WINDOW = DEFAULT_STATS_WINDOW;

struct WindowSums {
  uint64_t sumPeriod = 0;
  uint64_t sumPeriodUs = 0;
  double   sumSqPeriodUs = 0.0;
  double   sumBpm = 0.0;
  double   sumSqBpm = 0.0;
  int64_t  sumDeltaBeat = 0;
  int64_t  sumDeltaBlock = 0;
  double   sumSqDeltaBeat = 0.0;
  double   sumSqDeltaBlock = 0.0;
  uint64_t sumBlockJump = 0;
  double   sumSqBlockJump = 0.0;
  uint16_t blockJumpCount = 0;

  void reset() {
    sumPeriod = 0;
    sumPeriodUs = 0;
    sumSqPeriodUs = 0.0;
    sumBpm = 0.0;
    sumSqBpm = 0.0;
    sumDeltaBeat = 0;
    sumDeltaBlock = 0;
    sumSqDeltaBeat = 0.0;
    sumSqDeltaBlock = 0.0;
    sumBlockJump = 0;
    sumSqBlockJump = 0.0;
    blockJumpCount = 0;
  }

  void addSample(uint32_t period_units, uint32_t period_us, float bpm_now,
                 int32_t dBeat_units, int32_t dBlock_units,
                 uint32_t blockJumpMag, bool countBlockJump) {
    sumPeriod     += period_units;
    sumPeriodUs   += period_us;
    sumSqPeriodUs += (double)period_us * (double)period_us;
    sumBpm        += (double)bpm_now;
    sumSqBpm      += (double)bpm_now * (double)bpm_now;
    sumDeltaBeat  += dBeat_units;
    sumDeltaBlock += dBlock_units;
    sumSqDeltaBeat  += (double)dBeat_units  * (double)dBeat_units;
    sumSqDeltaBlock += (double)dBlock_units * (double)dBlock_units;
    if (countBlockJump) {
      sumBlockJump    += (uint64_t)blockJumpMag;
      sumSqBlockJump  += (double)blockJumpMag * (double)blockJumpMag;
      blockJumpCount++;
    }
  }

  void removeSample(uint32_t period_units, uint32_t period_us, double bpm,
                    int32_t dBeat_units, int32_t dBlock_units,
                    uint32_t blockJumpMag, bool hadPrevBlock) {
    sumPeriod      -= period_units;
    sumPeriodUs    -= period_us;
    sumSqPeriodUs  -= (double)period_us * (double)period_us;
    sumBpm         -= bpm;
    sumSqBpm       -= bpm * bpm;
    sumDeltaBeat   -= dBeat_units;
    sumDeltaBlock  -= dBlock_units;
    sumSqDeltaBeat  -= (double)dBeat_units  * (double)dBeat_units;
    sumSqDeltaBlock -= (double)dBlock_units * (double)dBlock_units;
    if (hadPrevBlock && blockJumpCount) {
      sumBlockJump   -= (uint64_t)blockJumpMag;
      sumSqBlockJump -= (double)blockJumpMag * (double)blockJumpMag;
      blockJumpCount--;
    }
  }

  void removeOrphanedHead(uint32_t blockJumpMag) {
    if (blockJumpCount) {
      sumBlockJump   -= (uint64_t)blockJumpMag;
      sumSqBlockJump -= (double)blockJumpMag * (double)blockJumpMag;
      blockJumpCount--;
    }
  }

  RollingStats finalize(uint16_t count, uint32_t period_us,
                        float bpm_now, int32_t dBeat_units,
                        int32_t dBlock_units, uint32_t blockJumpMag) const {
    RollingStats result = {0};
    if (count) {
      double c = (double)count;
      result.avg_bpm           = (float)(sumBpm / c);
      result.avg_delta_beat    = (float)((double)sumDeltaBeat  / c);
      result.avg_delta_block   = (float)((double)sumDeltaBlock / c);
      if (blockJumpCount) {
        double jumpCount = (double)blockJumpCount;
        result.avg_block_jump    = (float)((double)sumBlockJump / jumpCount);
        result.stddev_block_jump = stddev((double)sumBlockJump, sumSqBlockJump, blockJumpCount);
      } else {
        result.avg_block_jump    = 0.0f;
        result.stddev_block_jump = 0.0f;
      }
      result.avg_period_us     = (float)((double)sumPeriodUs / c);
      result.stddev_bpm        = stddev(sumBpm, sumSqBpm, count);
      result.stddev_delta_beat = stddev((double)sumDeltaBeat, sumSqDeltaBeat, count);
      result.stddev_delta_block= stddev((double)sumDeltaBlock, sumSqDeltaBlock, count);
      result.stddev_period_us  = stddev((double)sumPeriodUs, sumSqPeriodUs, count);
    } else {
      result.avg_bpm           = bpm_now;
      result.avg_delta_beat    = (float)dBeat_units;
      result.avg_delta_block   = (float)dBlock_units;
      result.avg_block_jump    = 0.0f;
      result.avg_period_us     = (float)period_us;
      result.stddev_bpm        = 0.0f;
      result.stddev_delta_beat = 0.0f;
      result.stddev_delta_block= 0.0f;
      result.stddev_block_jump = 0.0f;
      result.stddev_period_us  = 0.0f;
    }

    result.bpm         = bpm_now;
    result.delta_beat  = (float)dBeat_units;
    result.delta_block = (float)dBlock_units;
    return result;
  }
};

struct WindowState {
  RollingSample samples[MAX_WINDOW];
  WindowSums sums;
  RollingStats stats = {0};
  uint16_t capacity = 0;
  uint16_t index = 0;
  uint16_t tail = 0;
  uint16_t count = 0;
};

static WindowState window;
static uint16_t lastClampedRequest = 0;
static uint16_t lastRequestedWindow = 0;

static uint32_t adjustedTicksToMicros(uint32_t ticks, int32_t corr_ppm) {
  uint64_t adjusted = ((uint64_t)ticks * (CORR_PPM_SCALE + (int64_t)corr_ppm)) / CORR_PPM_SCALE;
  return (uint32_t)((adjusted * 1000000ULL) / 16000000ULL);
}

static float stddev(double sum, double sumSq, uint16_t count) {
  if (count == 0) return 0.0f;
  double mean = sum / (double)count;
  double variance = (sumSq / (double)count) - (mean * mean);
  if (variance < 0.0) variance = 0.0;
  return (float)std::sqrt(variance);
}

void reset() {
  uint16_t requestedWindow = UnoTunables::statsWindowSize;
  lastRequestedWindow = requestedWindow;
  window.capacity = requestedWindow;
  if (window.capacity > MAX_WINDOW) {
    if (requestedWindow != lastClampedRequest) {
      Display::scrollLog(F("Clamping statsWindowSize to 288"));
      lastClampedRequest = requestedWindow;
    }
    window.capacity = MAX_WINDOW;
  }
  window.index = 0;
  window.tail = 0;
  window.count = 0;
  window.stats = {0};
  window.sums.reset();
}

void update() {
  uint16_t requestedWindow = UnoTunables::statsWindowSize;
  uint16_t desiredCapacity = requestedWindow > MAX_WINDOW ? MAX_WINDOW : requestedWindow;
  if (window.capacity != desiredCapacity || requestedWindow != lastRequestedWindow) {
    reset();
  }
  unsigned long now = millis();
  PendulumSample sample;
  memcpy(&sample, &NanoComm::currentSample, sizeof(sample));
  uint32_t tick_units       = NanoComm::ticksToUnits(sample.tick, sample.corr_blend_ppm);
  uint32_t tock_units       = NanoComm::ticksToUnits(sample.tock, sample.corr_blend_ppm);
  uint32_t tick_block_units = NanoComm::ticksToUnits(sample.tick_block, sample.corr_blend_ppm);
  uint32_t tock_block_units = NanoComm::ticksToUnits(sample.tock_block, sample.corr_blend_ppm);
  uint32_t period_units     = tick_units + tock_units + tick_block_units + tock_block_units;
  uint32_t period_us        = adjustedTicksToMicros(sample.tick, sample.corr_blend_ppm) +
                              adjustedTicksToMicros(sample.tock, sample.corr_blend_ppm) +
                              adjustedTicksToMicros(sample.tick_block, sample.corr_blend_ppm) +
                              adjustedTicksToMicros(sample.tock_block, sample.corr_blend_ppm);
  if (period_units == 0) {
    return;
  }
  float bpm_now             = unitsPerMinute(sample.corr_blend_ppm) / period_units;
  int32_t dBeat_units       = (int32_t)tick_units       - (int32_t)tock_units;
  int32_t dBlock_units      = (int32_t)tick_block_units - (int32_t)tock_block_units;

  uint16_t cap = window.capacity;
  bool hasPrevSample = window.count > 0;
  uint32_t blockJumpMag = (uint32_t)std::abs(dBlock_units);
  if (hasPrevSample && UnoTunables::blockJumpUs > 0) {
    int32_t blockJump = 0;
    switch (NanoComm::getDataUnits()) {
      case DataUnits::RawCycles:
        blockJump = (int32_t)(((uint64_t)UnoTunables::blockJumpUs * 16ULL * (CORR_PPM_SCALE + sample.corr_blend_ppm)) / CORR_PPM_SCALE);
        break;
      case DataUnits::AdjustedMs:
        blockJump = UnoTunables::blockJumpUs / 1000;
        if (blockJump == 0 && UnoTunables::blockJumpUs > 0) blockJump = 1; // avoid zero-threshold resets
        break;
      case DataUnits::AdjustedNs: blockJump = UnoTunables::blockJumpUs * 1000; break;
      case DataUnits::AdjustedUs: default: blockJump = UnoTunables::blockJumpUs; break;
    }
    if (blockJumpMag > (uint32_t)blockJump) {
      reset();
      hasPrevSample = false;
      blockJumpMag = 0;
    }
  }

  RollingSample &slot = window.samples[window.index];
  slot.tick       = sample.tick;
  slot.tock       = sample.tock;
  slot.tick_block = sample.tick_block;
  slot.tock_block = sample.tock_block;
  slot.corr_blend_ppm = sample.corr_blend_ppm;
  slot.timestamp_ms  = now;
  slot.block_jump_mag = blockJumpMag;
  slot.has_prev_block = hasPrevSample;

  window.index = (window.index + 1) % cap;

  window.sums.addSample(period_units, period_us, bpm_now, dBeat_units, dBlock_units, blockJumpMag, hasPrevSample);
  if (window.count < cap) window.count++;

  while (window.count && (now - window.samples[window.tail].timestamp_ms) > UnoTunables::rollingWindowMs) {
    const RollingSample &old = window.samples[window.tail];
    uint32_t oldTick = NanoComm::ticksToUnits(old.tick, old.corr_blend_ppm);
    uint32_t oldTock = NanoComm::ticksToUnits(old.tock, old.corr_blend_ppm);
    uint32_t oldPeriod = oldTick + oldTock;
    int32_t  oldDBeat  = (int32_t)oldTick - (int32_t)oldTock;
    uint32_t oldTickBlk = NanoComm::ticksToUnits(old.tick_block, old.corr_blend_ppm);
    uint32_t oldTockBlk = NanoComm::ticksToUnits(old.tock_block, old.corr_blend_ppm);
    int32_t  oldDBlk   = (int32_t)oldTickBlk - (int32_t)oldTockBlk;
    oldPeriod      += oldTickBlk + oldTockBlk;
    uint32_t oldPeriodUs = adjustedTicksToMicros(old.tick, old.corr_blend_ppm) +
                           adjustedTicksToMicros(old.tock, old.corr_blend_ppm) +
                           adjustedTicksToMicros(old.tick_block, old.corr_blend_ppm) +
                           adjustedTicksToMicros(old.tock_block, old.corr_blend_ppm);
    float oldBpm = unitsPerMinute(old.corr_blend_ppm) / (float)oldPeriod;
    window.sums.removeSample(oldPeriod, oldPeriodUs, (double)oldBpm, oldDBeat, oldDBlk, old.block_jump_mag, old.has_prev_block);
    window.tail = (window.tail + 1) % cap;
    window.count--;
    if (window.count) {
      RollingSample &newHead = window.samples[window.tail];
      if (newHead.has_prev_block) {
        window.sums.removeOrphanedHead(newHead.block_jump_mag);
        newHead.has_prev_block = false;
        newHead.block_jump_mag = 0;
      }
    }
  }

  window.stats = window.sums.finalize(window.count, period_us, bpm_now, dBeat_units, dBlock_units, blockJumpMag);
}

const RollingStats &get() {
  return window.stats;
}

uint16_t windowCount() {
  return window.count;
}

uint16_t windowCapacityLimit() {
  return window.capacity;
}

} // namespace StatsEngine
