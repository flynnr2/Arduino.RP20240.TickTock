#pragma once

#include "Common.h"
#include "PendulumProtocol.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef AP_PASS
#define AP_PASS "TickTock"
#endif

// Display settings
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDR       0x3D

// Sensor polling cadence
constexpr uint32_t SENSOR_PERIOD_MS = 1000; // environmental sensor read interval

// Stats / buffering defaults
// Reduce default stats window to trim RAM usage on the Uno R4 while still
// providing several minutes of history at higher BPMs.
constexpr uint16_t DEFAULT_STATS_WINDOW  = 288;      // ~3.2 min @ 180 BPM (~288 cycles)
constexpr uint32_t DEFAULT_ROLLING_MS    = 300000UL; // 5 minutes

// Rolling stats thresholds / safety defaults
constexpr int32_t  DEFAULT_BLOCK_JUMP_US = 500;       // μs jump that triggers stats reset

// Rolling stats thresholds / safety
constexpr uint32_t MAX_PERIOD_US_EST    = 1000000UL; // conservative worst-case half-period sum
constexpr uint32_t MAX_DELTA_US_EST     = 1000000UL; // conservative worst-case delta

// SD settings
#define SD_CS_PIN          10
#define LOG_FILENAME       "pendulum.csv"
constexpr size_t LOG_FILENAME_LEN = 20;    // includes null terminator
constexpr bool   LOG_DAILY_DEFAULT   = false;
constexpr bool   LOG_ENABLED_DEFAULT = false;
constexpr bool   LOG_APPEND_DEFAULT  = false;

// Serial from Nano Every (baud rate in PendulumProtocol.h)
#define SERIAL_TIMEOUT_MS  50
#define NANO_LINE_MAX      256
#define NANO_SERIAL        Serial1

// EEPROM layout (512 bytes total)
// 0 - 63   : shared TunableConfig (slot A)
// 64 - 127 : shared TunableConfig (slot B)
// 128 - 191: UnoConfig (slot A)
// 192 - 255: UnoConfig (slot B)
// 256 - 383: WiFi credentials (slot 0)
// 384 - 511: WiFi credentials (slot 1)
#define EEPROM_SIZE            512
#define MAX_SSID_LEN            32
#define MAX_PASS_LEN            64

// EEPROM slots for shared and Uno-specific configurations
constexpr int EEPROM_UNO_SHARED_SLOT_A_ADDR = 0;                                       // TunableConfig slot A
constexpr int EEPROM_UNO_SHARED_SLOT_B_ADDR = EEPROM_UNO_SHARED_SLOT_A_ADDR + 64;      // TunableConfig slot B
constexpr int EEPROM_UNO_SLOT_A_ADDR        = EEPROM_UNO_SHARED_SLOT_B_ADDR + 64;      // UnoConfig slot A
constexpr int EEPROM_UNO_SLOT_B_ADDR        = EEPROM_UNO_SLOT_A_ADDR + 64;             // UnoConfig slot B
constexpr int EEPROM_WIFI_SLOT_SIZE         = 128;                                     // reserve 128 bytes per WiFi slot
constexpr int EEPROM_WIFI_SLOT0_ADDR        = 256;                                     // WiFi credentials slot 0
constexpr int EEPROM_WIFI_SLOT1_ADDR        = EEPROM_WIFI_SLOT0_ADDR + EEPROM_WIFI_SLOT_SIZE; // WiFi credentials slot 1
static_assert(EEPROM_WIFI_SLOT1_ADDR + EEPROM_WIFI_SLOT_SIZE <= EEPROM_SIZE, "WiFi slots must fit EEPROM");

// WiFi
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define AP_SSID                 "PendulumLoggerSetup"
#define HTTP_PORT               80
constexpr unsigned WIFI_AP_RESTART_BACKOFF_MS = 15000; // minimum delay between AP restarts
constexpr unsigned WIFI_AP_DROP_GRACE_MS = 30000;      // grace period before restarting AP after drop
constexpr unsigned WIFI_AP_END_DELAY_MS   = 100;   // wait after WiFi.end() before starting AP
constexpr unsigned WIFI_AP_START_DELAY_MS = 1500;  // allow AP mode to initialize
constexpr unsigned WIFI_AP_START_TIMEOUT_MS = 5000; // timeout while waiting for AP mode
constexpr unsigned WIFI_CONNECT_RETRY_MS  = 250;   // interval between WiFi status checks
constexpr unsigned WIFI_RECONNECT_INTERVAL_MS = 30000; // interval between reconnect attempts

// SD flush throttling
const uint16_t FLUSH_EVERY_N = 32;
const unsigned long FLUSH_EVERY_MS = 5000;

// RAM monitor thresholds
#define RAM_WARN_THRESHOLD   4000   // bytes
#define RAM_CRIT_THRESHOLD   2000

// Debug timing
#define DEBUG_TIMING 0

// Uno R4 specific defaults
constexpr uint8_t  RING_SIZE_DEFAULT         = 8;        // ring buffer for incoming samples
constexpr uint32_t MIN_SWING_INTERVAL_TICKS  = 100000u;
constexpr uint32_t PPS_MIN_US_DEFAULT        = 500000UL;
constexpr uint32_t PPS_MAX_US_DEFAULT        = 2000000UL;
constexpr uint16_t MIN_EDGE_SEP_TICKS        = 500u;

constexpr float    CORRECTION_JUMP_THRESH_DEFAULT = 50.0f;
constexpr uint8_t  TX_BATCH_DEFAULT               = 4;       // serial TX batch size
constexpr bool     PROTECT_SHARED_READS_DEFAULT   = true;
constexpr bool     ENABLE_METRICS_DEFAULT         = false;
constexpr uint32_t METRICS_PERIOD_MS_DEFAULT      = 5000u;
constexpr uint8_t  PPS_EMA_SHIFT_DEFAULT          = 6;

namespace Tunables {
  extern float    correctionJumpThresh;
  extern uint8_t  ppsEmaShift;
  extern DataUnits dataUnits;
}

namespace UnoTunables {
  extern uint32_t debounceTicks;
  extern uint32_t ppsMinUs;
  extern uint32_t ppsMaxUs;
  extern uint32_t metricsPeriodMs;

  extern uint16_t statsWindowSize;
  extern uint32_t rollingWindowMs;
  extern int32_t  blockJumpUs;

  extern uint16_t minEdgeSepTicks;

  extern uint8_t  txBatchSize;
  extern uint8_t  ringSize;
  extern bool     protectSharedReads;
  extern bool     enableMetrics;

  extern bool     logDaily;
  extern bool     logEnabled;
  extern bool     logAppend;
  extern char     logBaseName[LOG_FILENAME_LEN];
}

// Uno-specific tunables stored separately from shared TunableConfig
struct TunableConfig {
  float    correctionJumpThresh;
  uint32_t seq;      // monotonically increasing sequence to pick latest config
  uint16_t crc16;
  uint8_t  ppsEmaShift;
  uint8_t  dataUnits;
};

struct UnoConfig {
  uint32_t debounceTicks;
  uint32_t ppsMinUs;
  uint32_t ppsMaxUs;
  uint32_t metricsPeriodMs;
  uint32_t rollingWindowMs;
  int32_t  blockJumpUs;
  uint32_t seq;      // sequence for Uno-specific config

  uint16_t statsWindowSize;
  uint16_t minEdgeSepTicks;
  uint16_t crc16;

  uint8_t  txBatchSize;
  uint8_t  ringSize;
  bool     protectSharedReads;
  bool     enableMetrics;

  bool     logDaily;
  bool     logEnabled;
  bool     logAppend;
  char     logBaseName[LOG_FILENAME_LEN];
};
