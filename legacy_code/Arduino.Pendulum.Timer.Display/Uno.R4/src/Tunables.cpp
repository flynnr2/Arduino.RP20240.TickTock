#include "Config.h"

namespace Tunables {
  float    correctionJumpThresh = CORRECTION_JUMP_THRESH_DEFAULT;
  uint8_t  ppsEmaShift          = PPS_EMA_SHIFT_DEFAULT;
  DataUnits dataUnits           = DATA_UNITS_DEFAULT;
}

namespace UnoTunables {
  uint32_t debounceTicks        = MIN_SWING_INTERVAL_TICKS;
  uint32_t ppsMinUs             = PPS_MIN_US_DEFAULT;
  uint32_t ppsMaxUs             = PPS_MAX_US_DEFAULT;
  uint32_t metricsPeriodMs      = METRICS_PERIOD_MS_DEFAULT;

  uint16_t statsWindowSize      = DEFAULT_STATS_WINDOW;
  uint32_t rollingWindowMs      = DEFAULT_ROLLING_MS;
  int32_t  blockJumpUs          = DEFAULT_BLOCK_JUMP_US;

  uint16_t minEdgeSepTicks      = MIN_EDGE_SEP_TICKS;

  uint8_t  txBatchSize          = TX_BATCH_DEFAULT;
  uint8_t  ringSize             = RING_SIZE_DEFAULT;
  bool     protectSharedReads   = PROTECT_SHARED_READS_DEFAULT;
  bool     enableMetrics        = ENABLE_METRICS_DEFAULT;

  bool     logDaily             = LOG_DAILY_DEFAULT;
  bool     logEnabled           = LOG_ENABLED_DEFAULT;
  bool     logAppend            = LOG_APPEND_DEFAULT;
  char     logBaseName[LOG_FILENAME_LEN] = LOG_FILENAME;
}
