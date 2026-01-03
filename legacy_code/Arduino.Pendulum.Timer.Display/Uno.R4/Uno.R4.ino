// --------------------------------------------------------------
// UNO R4 WiFi Pendulum Logger
// Companion to Nano Every GPS-disciplined pendulum timer
// UNO R4 handles WiFi/AP config, SD logging, OLED UI, env sensors & rolling stats
// --------------------------------------------------------------

// --------------------------------------------------------------
// TODO
// --------------------------------------------------------------

// --------------------------------------------------------------
// Refactor & Optimization Ideas (concise)
// --------------------------------------------------------------
// - Wrap static strings with F() to store in flash and save RAM
// - Move HTML/JSON templates into PROGMEM and print with print_P() to reduce code duplication and RAM use
// - Cache environmental sensor reads at a fixed interval to minimize I2C latency per sample
// - Encapsulate rolling stats logic into a dedicated C++ class for better modularity and testability
// - Use buffered or binary block writes for SD to boost throughput when logging large datasets
// - Replace all String usage with snprintf() and fixed char buffers to prevent heap fragmentation
// - Split features into separate modules/files (WiFi, SDLogger, StatsEngine, OLEDDisplay, Sensors) for clarity and faster builds
// - Guard diagnostic code (I2C scan, detailed logging) behind a single DEBUG flag to strip in production builds
// - Consider low-power sleep modes between pendulum swings for improved energy efficiency on battery power

// --------------------------------------------------------------
// Suggested Additional Analyses
// --------------------------------------------------------------
// Calculate amplitude (require speed of pendulum (or width of penulum & pivot to senor length))
//
// Stability & Timing Analysis:
//  - Allan Deviation (σy(τ)): Compute rolling Allan deviation over various averaging intervals to characterize stability and noise types (white noise, drift, random walk).
//  - Drift Estimation: Calculate long-term frequency drift by comparing pendulum intervals to GPS reference over hours/days.
//
// Frequency Domain Analysis:
//  - FFT & Power Spectral Density (PSD): Detect and quantify periodic disturbances (e.g., mechanical resonances, electrical interference).
//
// Environmental Correlation:
//  - Analyze correlations between pendulum timing (BPM, period) and environmental conditions (temperature, humidity, pressure). Consider simple linear regression or correlation coefficients to detect significant influences.
//
// Outlier Detection & Histogram:
//  - Maintain a histogram of timing deviations to quickly detect non-Gaussian events or intermittent mechanical/electrical disturbances.
//
// Phase Noise (Advanced):
//  - If higher-resolution timestamps become available (e.g., via FPGA or TDC), compute and log phase noise spectra to benchmark timing accuracy and jitter performance.
//
// Logging & Telemetry Enhancements:
//  - Include computed Allan deviation and drift rate in HTTP JSON telemetry endpoint for easy real-time monitoring.

#include <EEPROM.h>
#include "src/secrets.h"
#include "src/Config.h"
#include "src/PendulumProtocol.h"
#include "src/WiFiConfig.h"
#include "src/HttpServer.h"
#include "src/SDLogger.h"
#include "src/Sensors.h"
#include "src/StatsEngine.h"
#include "src/Display.h"
#include "src/NanoComm.h"
#include "src/MemoryMonitor.h"
#include "src/EEPROMConfig.h"

void setup() {
  Serial.begin(115200);
  Wire.begin();

  NANO_SERIAL.begin(SERIAL_BAUD_NANO);
  NANO_SERIAL.setTimeout(SERIAL_TIMEOUT_MS);

  delay(1000);

  // EEPROM on UNO R4 does not require begin(size); call returns EEPtr
  EEPROM.begin();
  bool eepromReady = true;
  bool configLoaded = false;
  TunableConfig cfg = getCurrentConfig();
  UnoConfig ucfg = getCurrentUnoConfig();
  if (eepromReady && loadConfig(cfg, ucfg)) {
    applyConfig(cfg);
    applyUnoConfig(ucfg);
    configLoaded = true;
  }
  Display::begin();
  Display::showSplash();
  if (!eepromReady) {
    Display::scrollLog(F("EEPROM init failed; using defaults"));
  } else if (!configLoaded) {
    Display::scrollLog(F("No saved config; using defaults"));
  }
  WiFiConfig::begin();
  HttpServer::begin();
  SDLogger::begin();
  Sensors::begin();
  Sensors::scanI2C();
  NanoComm::readStartup();

  SDLogger::setLogMode(UnoTunables::logDaily ? SDLogger::LogMode::Daily : SDLogger::LogMode::Continuous);
  SDLogger::setFilename(UnoTunables::logBaseName);
  SDLogger::setAppendMode(UnoTunables::logAppend);
  if (UnoTunables::logEnabled) {
    SDLogger::startLogging(SDLogger::getLogMode(), false);
  }
}

void loop() {
  WiFiConfig::service();
  HttpServer::service();
  SDLogger::service();
  MemoryMonitor::poll();
  MemoryMonitor::serviceBlink();
  Sensors::poll();

  if (NanoComm::streamingStarted() && NANO_SERIAL.available()) {
    char line[NANO_LINE_MAX]; // incoming line buffer (max NANO_LINE_MAX-1 chars)
    size_t len = NANO_SERIAL.readBytesUntil('\n', line, sizeof(line)-1);
    line[len] = '\0';
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == ' ')) line[--len] = '\0';
    if (len && NanoComm::parseLine(line)) {
      float t,h,p; Sensors::getLatest(t,h,p);
      NanoComm::currentSample.temperature_C = t;
      NanoComm::currentSample.humidity_pct = h;
      NanoComm::currentSample.pressure_hPa = p;
      StatsEngine::update();
      SDLogger::logSample(NanoComm::currentSample);
    }
  }

  static unsigned long lastOled = 0;
  if (millis() - lastOled > 1000) {
    Display::update();
    lastOled = millis();
  }
}
