#include "Sensors.h"
#include "Display.h"
#include <cmath>

namespace Sensors {

static Adafruit_BMP280 bmp;
static Adafruit_SHT4x sht4 = Adafruit_SHT4x();

static float cachedTemperature = NAN;
static float cachedHumidity = NAN;
static float cachedPressure = NAN;
static uint32_t lastReadMs = 0;
static bool sensorsReady = false;
static bool warnedShtRead = false;
static bool warnedBmpRead = false;
static uint32_t lastUnavailableLogMs = 0;

static bool readTemperatureHumidity(float &temperature, float &humidity) {
  sensors_event_t hum, temp;
  if (sht4.getEvent(&hum, &temp)) {
    temperature = temp.temperature;
    humidity = hum.relative_humidity;
    return true;
  }
  return false;
}

static bool readPressure(float &pressure) {
  float p = bmp.readPressure();
  if (std::isfinite(p) && p > 0.0f) {
    pressure = p / 100.0f;
    return true;
  }
  return false;
}

void begin() {
  bool ok = true;
  if (!sht4.begin()) { Display::scrollLog(F("SHT4x not found")); ok = false; }
  if (!bmp.begin())  { Display::scrollLog(F("BMP280 not found")); ok = false; }
  sensorsReady = ok;
  if (ok) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    (void)readTemperatureHumidity(cachedTemperature, cachedHumidity);
    (void)readPressure(cachedPressure);
    lastReadMs = millis();
  }
}

void poll() {
  uint32_t now = millis();
  if (!sensorsReady) {
    if (now - lastUnavailableLogMs >= SENSOR_PERIOD_MS * 5) {
      Display::scrollLog(F("Sensors unavailable; polling paused"));
      lastUnavailableLogMs = now;
    }
    return;
  }
  if (now - lastReadMs >= SENSOR_PERIOD_MS) {
    float t = cachedTemperature;
    float h = cachedHumidity;
    float p = cachedPressure;
    bool shtOk = readTemperatureHumidity(t, h);
    bool bmpOk = readPressure(p);

    if (shtOk) {
      cachedTemperature = t;
      cachedHumidity = h;
      warnedShtRead = false;
    } else if (!warnedShtRead) {
      Display::scrollLog(F("SHT4x read failed"));
      warnedShtRead = true;
    }

    if (bmpOk) {
      cachedPressure = p;
      warnedBmpRead = false;
    } else if (!warnedBmpRead) {
      Display::scrollLog(F("BMP280 read failed"));
      warnedBmpRead = true;
    }

    lastReadMs = now;
  }
}

void getLatest(float &temperature, float &humidity, float &pressure) {
  temperature = cachedTemperature;
  humidity = cachedHumidity;
  pressure = cachedPressure;
}

void scanI2C() {
  Display::scrollLog(F("Scanning I2C..."));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char buf[12];
      snprintf(buf, sizeof(buf), "0x%02X", addr);
      Display::scrollLog(String("I2C: ") + buf);
      found++;
      delay(2);
    }
  }
  if (!found) {
    Display::scrollLog(F("No I2C devices"));
  }
}

} // namespace Sensors
