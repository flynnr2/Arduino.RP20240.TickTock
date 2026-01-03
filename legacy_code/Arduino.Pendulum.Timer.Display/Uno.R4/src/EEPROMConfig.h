#pragma once

#include "Config.h"

TunableConfig getCurrentConfig();
UnoConfig    getCurrentUnoConfig();

void applyConfig(const TunableConfig &cfg);
void applyUnoConfig(const UnoConfig &cfg);

bool loadConfig(TunableConfig &sharedOut, UnoConfig &unoOut);
void saveConfig(TunableConfig sharedCfg, UnoConfig unoCfg);
uint16_t computeCRC16(const uint8_t* data, size_t len);
