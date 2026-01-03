#include <EEPROM.h>
#include "Config.h"
#include "EEPROMConfig.h"

static_assert(sizeof(UnoConfig) <= 64, "UnoConfig must fit EEPROM slot");

uint16_t computeCRC16(const uint8_t* data, size_t len) {
  uint16_t crc = 0x0000;
  while (len--) {
    crc ^= (uint16_t)(*data++ << 8);
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
  }
  return crc;
}

static uint16_t crcConfig(TunableConfig cfg) {
  cfg.crc16 = 0;
  return computeCRC16(reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
}

static uint16_t crcUnoConfig(UnoConfig cfg) {
  cfg.crc16 = 0;
  return computeCRC16(reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
}

static uint32_t currentSeqShared = 0;
static uint32_t currentSeqUno    = 0;

TunableConfig getCurrentConfig() {
  TunableConfig cfg;
  cfg.correctionJumpThresh = Tunables::correctionJumpThresh;
  cfg.ppsEmaShift          = Tunables::ppsEmaShift;
  cfg.dataUnits            = static_cast<uint8_t>(Tunables::dataUnits);
  cfg.seq                  = currentSeqShared;
  cfg.crc16                = crcConfig(cfg);
  return cfg;
}

UnoConfig getCurrentUnoConfig() {
  UnoConfig cfg;
  cfg.debounceTicks      = UnoTunables::debounceTicks;
  cfg.ppsMinUs           = UnoTunables::ppsMinUs;
  cfg.ppsMaxUs           = UnoTunables::ppsMaxUs;
  cfg.metricsPeriodMs    = UnoTunables::metricsPeriodMs;
  cfg.statsWindowSize    = UnoTunables::statsWindowSize;
  cfg.rollingWindowMs    = UnoTunables::rollingWindowMs;
  cfg.blockJumpUs        = UnoTunables::blockJumpUs;
  cfg.minEdgeSepTicks    = UnoTunables::minEdgeSepTicks;
  cfg.txBatchSize        = UnoTunables::txBatchSize;
  cfg.ringSize           = UnoTunables::ringSize;
  cfg.protectSharedReads = UnoTunables::protectSharedReads;
  cfg.enableMetrics      = UnoTunables::enableMetrics;
  cfg.logDaily           = UnoTunables::logDaily;
  cfg.logEnabled         = UnoTunables::logEnabled;
  cfg.logAppend          = UnoTunables::logAppend;
  strncpy(cfg.logBaseName, UnoTunables::logBaseName, LOG_FILENAME_LEN);
  cfg.logBaseName[LOG_FILENAME_LEN-1] = 0;
  cfg.seq                = currentSeqUno;
  cfg.crc16              = crcUnoConfig(cfg);
  return cfg;
}

void applyConfig(const TunableConfig &cfg) {
  Tunables::correctionJumpThresh = cfg.correctionJumpThresh;
  Tunables::ppsEmaShift          = cfg.ppsEmaShift;
  Tunables::dataUnits            = static_cast<DataUnits>(cfg.dataUnits);
}
void applyUnoConfig(const UnoConfig &cfg) {
  UnoTunables::debounceTicks      = cfg.debounceTicks;
  UnoTunables::ppsMinUs           = cfg.ppsMinUs;
  UnoTunables::ppsMaxUs           = cfg.ppsMaxUs;
  UnoTunables::metricsPeriodMs    = cfg.metricsPeriodMs;
  UnoTunables::statsWindowSize    = cfg.statsWindowSize;
  UnoTunables::rollingWindowMs    = cfg.rollingWindowMs;
  UnoTunables::blockJumpUs        = cfg.blockJumpUs;
  UnoTunables::minEdgeSepTicks    = cfg.minEdgeSepTicks;
  UnoTunables::txBatchSize        = cfg.txBatchSize;
  UnoTunables::ringSize           = cfg.ringSize;
  UnoTunables::protectSharedReads = cfg.protectSharedReads;
  UnoTunables::enableMetrics      = cfg.enableMetrics;
  UnoTunables::logDaily           = cfg.logDaily;
  UnoTunables::logEnabled         = cfg.logEnabled;
  UnoTunables::logAppend          = cfg.logAppend;
  strncpy(UnoTunables::logBaseName, cfg.logBaseName, LOG_FILENAME_LEN);
  UnoTunables::logBaseName[LOG_FILENAME_LEN-1] = 0;
}

bool loadConfig(TunableConfig &out, UnoConfig &unoOut) {
  bool loaded = false;
  bool sharedLoaded = false;

  // --- Load shared tunables ---
  TunableConfig a, b;
  EEPROM.get(EEPROM_UNO_SHARED_SLOT_A_ADDR, a);
  EEPROM.get(EEPROM_UNO_SHARED_SLOT_B_ADDR, b);
  bool validA = (crcConfig(a) == a.crc16);
  bool validB = (crcConfig(b) == b.crc16);
  if (validA && validB) {
    out = (b.seq > a.seq) ? b : a;
    currentSeqShared = out.seq;
    loaded = sharedLoaded = true;
  } else if (validA) {
    out = a; currentSeqShared = a.seq; loaded = sharedLoaded = true;
  } else if (validB) {
    out = b; currentSeqShared = b.seq; loaded = sharedLoaded = true;
  }

  // --- Load Uno-specific tunables ---
  UnoConfig ua, ub;
  EEPROM.get(EEPROM_UNO_SLOT_A_ADDR, ua);
  EEPROM.get(EEPROM_UNO_SLOT_B_ADDR, ub);
  bool validUA = (crcUnoConfig(ua) == ua.crc16);
  bool validUB = (crcUnoConfig(ub) == ub.crc16);
  if (validUA && validUB) {
    unoOut = (ub.seq > ua.seq) ? ub : ua;
    currentSeqUno = unoOut.seq;
    loaded = true;
  } else if (validUA) {
    unoOut = ua; currentSeqUno = ua.seq; loaded = true;
  } else if (validUB) {
    unoOut = ub; currentSeqUno = ub.seq; loaded = true;
  }

  // Attempt migration from prior TunableConfig layout (ppsEmaShift/dataUnits before seq/crc)
  if (!sharedLoaded) {
    struct PriorTunableConfig {
      float    correctionJumpThresh;
      uint8_t  ppsEmaShift;
      uint8_t  dataUnits;
      uint32_t seq;
      uint16_t crc16;
    };

    auto crcPriorConfig = [](PriorTunableConfig cfg) {
      cfg.crc16 = 0;
      return computeCRC16(reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
    };

    PriorTunableConfig pa, pb;
    EEPROM.get(EEPROM_UNO_SHARED_SLOT_A_ADDR, pa);
    EEPROM.get(EEPROM_UNO_SHARED_SLOT_B_ADDR, pb);

    bool validPA = (crcPriorConfig(pa) == pa.crc16);
    bool validPB = (crcPriorConfig(pb) == pb.crc16);

    if (validPA || validPB) {
      PriorTunableConfig best = (validPB && (!validPA || pb.seq > pa.seq)) ? pb : pa;

      out.correctionJumpThresh = best.correctionJumpThresh;
      out.ppsEmaShift          = best.ppsEmaShift;
      out.dataUnits            = best.dataUnits;
      out.seq                  = best.seq;
      out.crc16                = crcConfig(out);

      currentSeqShared = best.seq;
      sharedLoaded = loaded = true;

      // Persist in new layout alongside any Uno config already loaded
      saveConfig(out, loaded ? unoOut : UnoConfig{});
      // update seq in returned structs in case saveConfig bumped it
      out.seq = currentSeqShared;
    }
  }

  if (loaded) return true;

  // --- Migration from legacy combined struct ---
  struct LegacyConfig {
    uint32_t debounceTicks;
    uint32_t ppsMinUs;
    uint32_t ppsMaxUs;
    uint32_t metricsPeriodMs;
    float    correctionJumpThresh;
    uint16_t statsWindowSize;
    uint32_t rollingWindowMs;
    int32_t  blockJumpUs;
    uint16_t minEdgeSepTicks;
    uint8_t  txBatchSize;
    uint8_t  ringSize;
    uint8_t  ppsEmaShift;
    uint8_t  dataUnits;
    bool     protectSharedReads;
    bool     enableMetrics;
    uint32_t seq;
    uint16_t crc16;
  };

  auto crcLegacy = [](LegacyConfig cfg) {
    cfg.crc16 = 0;
    return computeCRC16(reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
  };

  LegacyConfig la, lb;
  EEPROM.get(0, la);
  EEPROM.get(64, lb);
  bool validLA = (crcLegacy(la) == la.crc16);
  bool validLB = (crcLegacy(lb) == lb.crc16);
  if (validLA || validLB) {
    LegacyConfig best = (validLB && (!validLA || lb.seq > la.seq)) ? lb : la;

    out.correctionJumpThresh = best.correctionJumpThresh;
    out.ppsEmaShift          = best.ppsEmaShift;
    out.dataUnits            = best.dataUnits;
    out.seq                  = best.seq;
    out.crc16                = crcConfig(out);

    unoOut.debounceTicks      = best.debounceTicks;
    unoOut.ppsMinUs           = best.ppsMinUs;
    unoOut.ppsMaxUs           = best.ppsMaxUs;
    unoOut.metricsPeriodMs    = best.metricsPeriodMs;
    unoOut.statsWindowSize    = best.statsWindowSize;
    unoOut.rollingWindowMs    = best.rollingWindowMs;
    unoOut.blockJumpUs        = best.blockJumpUs;
    unoOut.minEdgeSepTicks    = best.minEdgeSepTicks;
    unoOut.txBatchSize        = best.txBatchSize;
    unoOut.ringSize           = best.ringSize;
    unoOut.protectSharedReads = best.protectSharedReads;
    unoOut.enableMetrics      = best.enableMetrics;
    unoOut.logDaily           = LOG_DAILY_DEFAULT;
    unoOut.logEnabled         = LOG_ENABLED_DEFAULT;
    unoOut.logAppend          = LOG_APPEND_DEFAULT;
    strncpy(unoOut.logBaseName, LOG_FILENAME, LOG_FILENAME_LEN);
    unoOut.logBaseName[LOG_FILENAME_LEN-1] = 0;
    unoOut.seq                = best.seq;
    unoOut.crc16              = crcUnoConfig(unoOut);

    currentSeqShared = best.seq;
    currentSeqUno    = best.seq;

    // Save in new layout
    saveConfig(out, unoOut);
    // update seq in returned structs
    out.seq     = currentSeqShared;
    unoOut.seq  = currentSeqUno;
    return true;
  }

  currentSeqShared = currentSeqUno = 0;
  return false;
}

void saveConfig(TunableConfig cfg, UnoConfig unoCfg) {
  static bool toggle = false;
  cfg.seq = ++currentSeqShared;
  cfg.crc16 = crcConfig(cfg);
  unoCfg.seq = ++currentSeqUno;
  unoCfg.crc16 = crcUnoConfig(unoCfg);

  int addrShared = toggle ? EEPROM_UNO_SHARED_SLOT_A_ADDR : EEPROM_UNO_SHARED_SLOT_B_ADDR;
  int addrUno    = toggle ? EEPROM_UNO_SLOT_A_ADDR : EEPROM_UNO_SLOT_B_ADDR;
  EEPROM.put(addrShared, cfg);
  EEPROM.put(addrUno, unoCfg);
  toggle = !toggle;
}
