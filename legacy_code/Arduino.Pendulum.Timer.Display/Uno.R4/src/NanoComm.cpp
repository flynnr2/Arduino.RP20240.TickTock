#include "NanoComm.h"
#include "Display.h"
#include "SDLogger.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

namespace NanoComm {

PendulumSample currentSample = {0};
static bool csvStreamingStarted = false;
static char csvHeader[256];

static DataUnits dataUnits = DATA_UNITS_DEFAULT;
static constexpr uint32_t NANO_TICK_FREQ = 16000000UL;

static const char* dataUnitsLabel() {
  switch (dataUnits) {
    case DataUnits::RawCycles:  return "cycles";
    case DataUnits::AdjustedMs: return "ms";
    case DataUnits::AdjustedUs: return "us";
    case DataUnits::AdjustedNs: return "ns";
    default: return "units";
  }
}

static bool isNumericToken(const char* tok) {
  if (!tok || !*tok) return false;
  if (*tok == '-' || *tok == '+') tok++;
  if (!*tok) return false;
  for (const char* p = tok; *p; ++p) {
    if (!isdigit((unsigned char)*p)) return false;
  }
  return true;
}

static bool handleUnitsTag(const char* tok) {
  if (strcmp(tok, TAG_16MHZ) == 0) { dataUnits = DataUnits::RawCycles;  return true; }
  if (strcmp(tok, TAG_NS)   == 0) { dataUnits = DataUnits::AdjustedNs; return true; }
  if (strcmp(tok, TAG_US)   == 0) { dataUnits = DataUnits::AdjustedUs; return true; }
  if (strcmp(tok, TAG_MS)   == 0) { dataUnits = DataUnits::AdjustedMs; return true; }
  return false;
}

// Convert a value expressed in the current data units to raw ticks while
// applying a ppm correction factor.  The Nano provides times already adjusted
// by its blended correction value, so we must scale the nominal tick frequency
// accordingly before converting to ticks.
static uint32_t unitsToTicks(uint32_t v, int32_t corr_ppm) {
  // Scale the nominal tick frequency by the correction factor.
  uint64_t adj_freq = ((uint64_t)NANO_TICK_FREQ *
                       (CORR_PPM_SCALE + corr_ppm)) / CORR_PPM_SCALE;
  switch (dataUnits) {
    case DataUnits::RawCycles:
      return v;
    case DataUnits::AdjustedMs:
      return (uint32_t)(((uint64_t)v * adj_freq) / 1000ULL);
    case DataUnits::AdjustedUs:
      return (uint32_t)(((uint64_t)v * adj_freq) / 1000000ULL);
    case DataUnits::AdjustedNs:
      return (uint32_t)(((uint64_t)v * adj_freq) / 1000000000ULL);
    default:
      return v;
  }
}

static void buildCsvHeader() {
  const char* units = dataUnitsLabel();
  snprintf(csvHeader, sizeof(csvHeader),
           "tick_%s,tock_%s,tick_block_%s,tock_block_%s,corr_inst_ppm,corr_blend_ppm,gps_status,dropped,temperature_C,humidity_pct,pressure_hPa",
           units, units, units, units);
}

uint32_t ticksToMicros(uint32_t ticks) {
  return (uint32_t)(((uint64_t)ticks * 1000000ULL) / NANO_TICK_FREQ);
}

float ticksToMs(uint32_t ticks, int32_t corr_ppm) {
  double adjusted =
      (double)ticks * ((double)CORR_PPM_SCALE + (double)corr_ppm) /
      (double)CORR_PPM_SCALE;
  return (float)((adjusted * 1000.0) / (double)NANO_TICK_FREQ);
}

uint32_t ticksToUnits(uint32_t ticks, int32_t corr_blend_ppm) {
  uint64_t adjusted = ((uint64_t)ticks * (CORR_PPM_SCALE + (int64_t)corr_blend_ppm)) / CORR_PPM_SCALE;
  switch (dataUnits) {
    case DataUnits::RawCycles:
      return (uint32_t)adjusted;
    case DataUnits::AdjustedMs:
      return (uint32_t)((adjusted * 1000ULL) / NANO_TICK_FREQ);
    case DataUnits::AdjustedUs:
      return (uint32_t)((adjusted * 1000000ULL) / NANO_TICK_FREQ);
    case DataUnits::AdjustedNs:
      return (uint32_t)((adjusted * 1000000000ULL) / NANO_TICK_FREQ);
    default:
      return (uint32_t)((adjusted * 1000000ULL) / NANO_TICK_FREQ);
  }
}

DataUnits getDataUnits() { return dataUnits; }

bool streamingStarted() { return csvStreamingStarted; }

bool parseLine(const char* line) {
  static char parseBuf[NANO_LINE_MAX]; // working buffer for tokenization
  size_t n = strnlen(line, sizeof(parseBuf));
  if (n >= sizeof(parseBuf)) return false;
  memcpy(parseBuf, line, n);
  parseBuf[n] = '\0';
  char* ctx = nullptr;
  char* tok = strtok_r(parseBuf, ",", &ctx);
  int fieldIndex = 0;
  // Hold raw numeric values until all correction factors are known.
  uint32_t tick_raw = 0;
  uint32_t tock_raw = 0;
  uint32_t tick_block_raw = 0;
  uint32_t tock_block_raw = 0;
  int32_t corr_inst_ppm_tmp = 0;
  int32_t corr_blend_ppm_tmp = 0;
  while (tok && fieldIndex < CF_COUNT) {
    if (strcmp(tok, TAG_DAT) == 0) { tok = strtok_r(nullptr, ",", &ctx); continue; }
    if (handleUnitsTag(tok))       { tok = strtok_r(nullptr, ",", &ctx); continue; }
    if (!isNumericToken(tok)) return false;
    switch (fieldIndex) {
      case CF_TICK:       tick_raw       = strtoul(tok, nullptr, 10); break;
      case CF_TOCK:       tock_raw       = strtoul(tok, nullptr, 10); break;
      case CF_TICK_BLOCK: tick_block_raw = strtoul(tok, nullptr, 10); break;
      case CF_TOCK_BLOCK: tock_block_raw = strtoul(tok, nullptr, 10); break;
      case CF_CORR_INST_PPM:  corr_inst_ppm_tmp  = (int32_t)strtol(tok, nullptr, 10); break;
      case CF_CORR_BLEND_PPM: corr_blend_ppm_tmp = (int32_t)strtol(tok, nullptr, 10); break;
      case CF_GPS_STATUS:    currentSample.gps_status            = (GpsStatus)atoi(tok); break;
      case CF_DROPPED:       currentSample.dropped_events        = (uint16_t)atoi(tok); break;
      default: return false;
    }
    fieldIndex++;
    tok = strtok_r(nullptr, ",", &ctx);
  }
  currentSample.corr_inst_ppm  = corr_inst_ppm_tmp;
  currentSample.corr_blend_ppm = corr_blend_ppm_tmp;
  currentSample.tick       = unitsToTicks(tick_raw, corr_blend_ppm_tmp);
  currentSample.tock       = unitsToTicks(tock_raw, corr_blend_ppm_tmp);
  currentSample.tick_block = unitsToTicks(tick_block_raw, corr_blend_ppm_tmp);
  currentSample.tock_block = unitsToTicks(tock_block_raw, corr_blend_ppm_tmp);
  return (fieldIndex == CF_COUNT);
}

void readStartup() {
  unsigned long start = millis();
  bool gotMeta = false;
  while (millis() - start < 5000) {
    while (NANO_SERIAL.available()) {
      char line[NANO_LINE_MAX]; // incoming line buffer
      size_t len = NANO_SERIAL.readBytesUntil('\n', line, sizeof(line)-1);
      line[len] = '\0';
      while (len > 0 && (line[len-1] == '\r' || line[len-1] == ' ')) line[--len] = '\0';
      if (!len) continue;
      if (!gotMeta) {
        char lower[NANO_LINE_MAX];
        strncpy(lower, line, sizeof(lower)-1);
        lower[sizeof(lower)-1] = '\0';
        for (size_t i=0; lower[i]; i++) lower[i] = tolower(lower[i]);
        if      (strstr(lower, "cycles")) dataUnits = DataUnits::RawCycles;
        else if (strstr(lower, "ms"))     dataUnits = DataUnits::AdjustedMs;
        else if (strstr(lower, "us"))     dataUnits = DataUnits::AdjustedUs;
        else if (strstr(lower, "ns"))     dataUnits = DataUnits::AdjustedNs;
        else {
          char cmd[32];
          snprintf(cmd, sizeof(cmd), "set %s adjusted_us", PARAM_DATA_UNITS);
          NANO_SERIAL.println(cmd);
          dataUnits = DataUnits::AdjustedUs;
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "Nano: %s", line);
        Display::scrollLog(msg);
        gotMeta = true;
        continue;
      }
      // normalize column names
      for (char* p = line; (p = strstr(p, "tick_us"));)  { memmove(p+4, p+8, strlen(p+8)+1); memcpy(p, "tick",4); }
      for (char* p = line; (p = strstr(p, "tock_us"));)  { memmove(p+4, p+8, strlen(p+8)+1); memcpy(p, "tock",4); }
      for (char* p = line; (p = strstr(p, "tick_block_us"));) { memmove(p+10, p+14, strlen(p+14)+1); memcpy(p, "tick_block",10); }
      for (char* p = line; (p = strstr(p, "tock_block_us"));) { memmove(p+10, p+14, strlen(p+14)+1); memcpy(p, "tock_block",10); }
      buildCsvHeader();
      if (SDLogger::ready()) {
        SDLogger::writeHeader(csvHeader);
      }
      csvStreamingStarted = true;
      return;
    }
  }

  // Fallback: start streaming even if metadata/header never arrived.
  if (!csvStreamingStarted) {
    if (!gotMeta) {
      dataUnits = DATA_UNITS_DEFAULT;
      Display::scrollLog(F("Nano metadata missing; using defaults"));
    } else {
      Display::scrollLog(F("Nano header missing; using defaults"));
    }
    buildCsvHeader();
    if (SDLogger::ready()) {
      SDLogger::writeHeader(csvHeader);
    }
    csvStreamingStarted = true;
  }
}

const char* getCSVHeader() { return csvHeader; }

} // namespace NanoComm
