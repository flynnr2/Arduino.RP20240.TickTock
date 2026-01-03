#include "HttpServer.h"
#include "WiFiStorage.h"
#include "SDLogger.h"
#include "Display.h"
#include "EEPROMConfig.h"
#include "NanoComm.h"
#include "StatsEngine.h"
#include "WiFiConfig.h"
#include "ArduinoHttpServer.h"
#include <SD.h>
#include <avr/pgmspace.h>
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

using ArduinoHttpServer::Method;
using ArduinoHttpServer::Request;
using ArduinoHttpServer::Response;
using HttpMethod = Method;
using HttpRequest = Request;
using HttpResponse = Response;

namespace HttpServer {


static const char HOME_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><title>Home</title></head><body><h2>UNO R4 Pendulum Logger</h2><ul><li><a href='/json'>JSON Sample</a></li><li><a href='/uno'>UNO Tunables</a></li><li><a href='/nano'>Nano Tunables</a></li><li><a href='/stats'>Stats</a></li><li><a href='/log'>Logging</a></li><li><a href='/wifi'>WiFi Config</a></li></ul><hr><small>UNO R4 Pendulum Logger</small></body></html>)rawliteral";

static const char NOT_FOUND_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><title>Not Found</title></head><body><h2>404 - Not Found</h2><p>The requested resource could not be located.</p><a href='/' aria-label='Return to home page'>Home</a><hr><small>UNO R4 Pendulum Logger</small></body></html>)rawliteral";

static const char STATS_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><title>Stats</title><style>body{font-family:sans-serif;font-size:14px;}#meta{margin-bottom:8px;}pre{background:#f6f8fa;padding:8px;}</style></head><body><h2>Pendulum Stats</h2><div id='meta'>Loading...</div><pre id='vals'></pre><pre id='roll'></pre><script>function formatNum(v,dec){return isFinite(v)?v.toFixed(dec):'--';}function formatSig(v,sig){return isFinite(v)?Number(v).toPrecision(sig):'--';}function update(){Promise.all([fetch('/json'),fetch('/stats.json')]).then(r=>Promise.all(r.map(x=>x.json()))).then(([sample,stats])=>{const tick=+sample.tick_us,tock=+sample.tock_us,tb=+sample.tick_block_us,sb=+sample.tock_block_us;const period=tick+tb+tock+sb;const bpmNow=period?60000000/period:0;const cap=stats.window_capacity||stats.window_size;const requested=stats.window_size;const windowLabel=cap===requested?`${cap}`:`${cap} (requested ${requested})`;document.getElementById('meta').textContent=`Data units: ${stats.data_units} | window: ${stats.samples}/${windowLabel} samples (${stats.rolling_window_ms} ms) | block jump reset: ${stats.block_jump_us} µs`;document.getElementById('vals').textContent=`tick_us: ${tick}\ntock_us: ${tock}\ntick_block_us: ${tb}\ntock_block_us: ${sb}\nperiod_us: ${period}\n\ndelta_beat_us: ${tick-tock}\ndelta_block_us: ${tb-sb}\nbpm: ${formatNum(bpmNow,2)}\ncorr_inst_ppm: ${sample.corr_inst_ppm}\ncorr_blend_ppm: ${sample.corr_blend_ppm}\ngps_status: ${sample.gps_status}\ndropped_events: ${sample.dropped_events}\ntemperature_C: ${formatNum(sample.temperature_C,2)}\nhumidity_pct: ${formatNum(sample.humidity_pct,2)}\npressure_hPa: ${formatNum(sample.pressure_hPa,2)}`;document.getElementById('roll').textContent=`Rolling stats (avg over latest ${stats.samples} samples)\navg_bpm: ${formatSig(stats.avg_bpm,5)}\nstddev_bpm: ${formatSig(stats.stddev_bpm,5)}\navg_period_us: ${formatNum(stats.avg_period_us,1)}\nstddev_period_us: ${formatNum(stats.stddev_period_us,1)}\navg_delta_beat (${stats.data_units}): ${formatNum(stats.avg_delta_beat,1)}\nstddev_delta_beat (${stats.data_units}): ${formatNum(stats.stddev_delta_beat,1)}\navg_delta_block (${stats.data_units}): ${formatNum(stats.avg_delta_block,1)}\nstddev_delta_block (${stats.data_units}): ${formatNum(stats.stddev_delta_block,1)}\navg_block_jump (${stats.data_units}): ${formatNum(stats.avg_block_jump,1)}\nstddev_block_jump (${stats.data_units}): ${formatNum(stats.stddev_block_jump,1)}`;}).catch(()=>{document.getElementById('meta').textContent='Waiting for samples...';});}setInterval(update,1000);update();</script><a href='/'>Home</a><hr><small>UNO R4 Pendulum Logger</small></body></html>)rawliteral";

static const char* dataUnitsLabel() {
  switch (NanoComm::getDataUnits()) {
    case DataUnits::RawCycles:  return "raw_cycles";
    case DataUnits::AdjustedMs: return "ms";
    case DataUnits::AdjustedUs: return "us";
    case DataUnits::AdjustedNs: return "ns";
    default: return "unknown";
  }
}

static WiFiServer tcpServer(HTTP_PORT);
static ArduinoHttpServer::Server httpServer(tcpServer);

static void urlDecode(char* s) {
  char* src = s;
  char* dst = s;
  while (*src) {
    if (*src == '+') { *dst++ = ' '; src++; }
    else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
      char a = toupper(src[1]);
      char b = toupper(src[2]);
      a = (a>='A') ? (a-'A'+10) : (a-'0');
      b = (b>='A') ? (b-'A'+10) : (b-'0');
      *dst++ = char(16*a + b);
      src += 3;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static bool getParam(const String& source, const char* key, char* out, size_t outLen) {
  int pos = source.indexOf(key);
  if (pos < 0) return false;
  pos += strlen(key);
  size_t i = 0;
  while (pos < (int)source.length() && source[pos] != '&' && i < outLen - 1) {
    out[i++] = source[pos++];
  }
  out[i] = '\0';
  urlDecode(out);
  return true;
}

class QueryParams {
public:
  explicit QueryParams(const String& src) : source(src) {}

  bool empty() const { return source.length() == 0; }

  bool copyValue(const char* key, char* out, size_t outLen) const {
    return getParam(source, key, out, outLen);
  }

  long toLong(const char* key, long defaultVal = 0) const {
    char buf[16] = {0};
    return copyValue(key, buf, sizeof(buf)) ? atol(buf) : defaultVal;
  }

  bool equals(const char* key, const char* value) const {
    char buf[32] = {0};
    return copyValue(key, buf, sizeof(buf)) && strcmp(buf, value) == 0;
  }

  bool flagEnabled(const char* key) const { return toLong(key, 0) != 0; }

private:
  const String& source;
};

static size_t clampJsonLength(int len, size_t bufSize) {
  if (len < 0) return 0;
  if ((size_t)len >= bufSize) return bufSize - 1;
  return (size_t)len;
}

static void sendBufferedJson(HttpResponse& response, const char* buf, size_t len) {
  response.setStatusCode(F("200 OK"));
  response.setHeader("Content-Type", "application/json");
  response.setHeader("Content-Length", String(len));
  response.setHeader("Connection", "close");
  response.beginBody(len);
  response.write((const uint8_t*)buf, len);
}

static void sendProgmemHtml(HttpResponse& response, const char* bodyProgmem, int statusCode = 200) {
  size_t bodyLen = strlen_P((PGM_P)bodyProgmem);
  response.setStatusCode(statusCode);
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Content-Length", String(bodyLen));
  response.setHeader("Connection", "close");
  response.beginBody(bodyLen);
  response.write_P((PGM_P)bodyProgmem, bodyLen);
}

static void sendNotFound(HttpResponse& response) {
  sendProgmemHtml(response, NOT_FOUND_PAGE, 404);
}

static void sendStatsJson(HttpResponse& response) {
  const RollingStats &st = StatsEngine::get();
  char buf[512];
  int len = snprintf(buf, sizeof(buf),
    "{\"bpm\":%.3f,\"delta_beat\":%.3f,\"delta_block\":%.3f,\"avg_bpm\":%.5g,\"avg_delta_beat\":%.3f,\"avg_delta_block\":%.3f,\"avg_block_jump\":%.3f,\"avg_period_us\":%.1f,\"stddev_bpm\":%.5g,\"stddev_delta_beat\":%.3f,\"stddev_delta_block\":%.3f,\"stddev_block_jump\":%.3f,\"stddev_period_us\":%.1f,\"samples\":%u,\"window_size\":%u,\"window_capacity\":%u,\"rolling_window_ms\":%lu,\"block_jump_us\":%ld,\"data_units\":\"%s\"}\n",
    st.bpm,
    st.delta_beat,
    st.delta_block,
    st.avg_bpm,
    st.avg_delta_beat,
    st.avg_delta_block,
    st.avg_block_jump,
    st.avg_period_us,
    st.stddev_bpm,
    st.stddev_delta_beat,
    st.stddev_delta_block,
    st.stddev_block_jump,
    st.stddev_period_us,
    (unsigned int)StatsEngine::windowCount(),
    (unsigned int)UnoTunables::statsWindowSize,
    (unsigned int)StatsEngine::windowCapacityLimit(),
    (unsigned long)UnoTunables::rollingWindowMs,
    (long)UnoTunables::blockJumpUs,
    dataUnitsLabel());
  size_t jsonLen = clampJsonLength(len, sizeof(buf));
  sendBufferedJson(response, buf, jsonLen);
}

static void sendJSON(HttpResponse& response) {
  char buf[256];
  PendulumSample sample;
  memcpy(&sample, &NanoComm::currentSample, sizeof(sample));
  int len = snprintf(buf, sizeof(buf),
    "{\"tick_us\":%lu,\"tock_us\":%lu,\"tick_block_us\":%lu,\"tock_block_us\":%lu,\"corr_inst_ppm\":%ld,\"corr_blend_ppm\":%ld,\"gps_status\":%u,\"dropped_events\":%u,\"temperature_C\":%.2f,\"humidity_pct\":%.2f,\"pressure_hPa\":%.2f}\n",
    (unsigned long)NanoComm::ticksToMicros(sample.tick),
    (unsigned long)NanoComm::ticksToMicros(sample.tock),
    (unsigned long)NanoComm::ticksToMicros(sample.tick_block),
    (unsigned long)NanoComm::ticksToMicros(sample.tock_block),
    (long)sample.corr_inst_ppm,
    (long)sample.corr_blend_ppm,
    (unsigned int)sample.gps_status,
    (unsigned int)sample.dropped_events,
    sample.temperature_C,
    sample.humidity_pct,
    sample.pressure_hPa);
  size_t jsonLen = clampJsonLength(len, sizeof(buf));
  sendBufferedJson(response, buf, jsonLen);
}

static void handleJsonRequest(HttpRequest& request, HttpResponse& response) {
  (void)request;
  sendJSON(response);
}

static void handleStatsJsonRequest(HttpRequest& request, HttpResponse& response) {
  (void)request;
  sendStatsJson(response);
}

static void sendHomePage(HttpResponse& response) {
  sendProgmemHtml(response, HOME_PAGE);
}

static void handleHomePage(HttpRequest& request, HttpResponse& response) {
  (void)request;
  sendHomePage(response);
}

static void renderLoggingPage(HttpResponse& response) {
  response.setStatusCode(F("200 OK"));
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Connection", "close");
  response.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>Logging</title></head><body>"));
  response.println(F("<h2>Logging Control</h2>"));
  response.print(F("<p>Status: "));
  response.print(SDLogger::isLogging() ? F("Logging") : F("Stopped"));
  response.print(F(" | Mode: "));
  response.print(SDLogger::getLogMode() == SDLogger::LogMode::Daily ? F("Daily rollover") : F("Continuous"));
  response.print(F(" | Active file: "));
  response.print(SDLogger::getActiveFilename());
  response.print(F(" | Append: "));
  response.print(SDLogger::getAppendMode() ? F("yes") : F("no"));
  response.println(F("</p>"));

  response.print(F("<p>Time sync: "));
  if (SDLogger::hasTimeSync()) {
    response.print(F("OK (age "));
    response.print(SDLogger::secondsSinceLastSync());
    response.println(F(" s)"));
  } else {
    response.println(F("waiting for NTP"));
  }
  response.println(F("</p>"));

  response.println(F("<h3>Settings</h3>"));
  response.println(F("<form action='/log' method='get'>"));
  response.println(F("Mode: <select name='mode'>"));
  response.print(F("<option value='continuous'")); if (SDLogger::getLogMode() == SDLogger::LogMode::Continuous) response.print(F(" selected")); response.println(F(">Continuous</option>"));
  response.print(F("<option value='daily'")); if (SDLogger::getLogMode() == SDLogger::LogMode::Daily) response.print(F(" selected")); response.println(F(">Daily rollover</option></select><br>"));
  response.print(F("Base filename (continuous): <input name='file' value='")); response.print(SDLogger::getFilename()); response.println(F("'><br>"));
  response.print(F("Append: <input name='append' type='number' min='0' max='1' value='")); response.print(SDLogger::getAppendMode() ? 1 : 0); response.println(F("'><br>"));
  response.println(F("<input type='submit' value='Save Settings'></form>"));

  response.println(F("<h3>Actions</h3>"));
  response.println(F("<form action='/log' method='get'><input type='hidden' name='log' value='1'><input type='submit' value='Start Logging'></form>"));
  response.println(F("<form action='/log' method='get'><input type='hidden' name='log' value='0'><input type='submit' value='Stop Logging'></form>"));
  response.println(F("<form action='/log' method='get'><input type='hidden' name='restart' value='1'><input type='submit' value='Restart (same file)'></form>"));
  response.println(F("<form action='/log' method='get'><input type='hidden' name='restart' value='new'><input type='submit' value='Restart with new file'></form>"));

  response.println(F("<p><a href='/logfiles'>Log files</a> | <a href='/' aria-label='Return to home page'>Home</a></p>"));
  response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
}

static void handleLogRequest(HttpRequest& request, HttpResponse& response) {
  String queryStr = request.query();
  QueryParams query(queryStr);
  bool hasQuery = !query.empty();

  TunableConfig shared = getCurrentConfig();
  UnoConfig unoCfg = getCurrentUnoConfig();

  SDLogger::setLogMode(unoCfg.logDaily ? SDLogger::LogMode::Daily : SDLogger::LogMode::Continuous);
  SDLogger::setAppendMode(unoCfg.logAppend);
  SDLogger::setFilename(unoCfg.logBaseName);

  bool startCmd = false, stopCmd = false, restartCmd = false, restartNew = false;
  char val[LOG_FILENAME_LEN] = {0};

  if (hasQuery) {
    char buf[32] = {0};
    if (query.copyValue("mode=", buf, sizeof(buf))) {
      if (strcmp(buf, "daily") == 0) { unoCfg.logDaily = true; SDLogger::setLogMode(SDLogger::LogMode::Daily); }
      else { unoCfg.logDaily = false; SDLogger::setLogMode(SDLogger::LogMode::Continuous); }
    }
    if (query.copyValue("append=", buf, sizeof(buf))) {
      unoCfg.logAppend = atoi(buf);
      SDLogger::setAppendMode(unoCfg.logAppend);
    }
    if (query.copyValue("file=", val, sizeof(val))) {
      SDLogger::setFilename(val);
      strncpy(unoCfg.logBaseName, SDLogger::getFilename(), LOG_FILENAME_LEN);
      unoCfg.logBaseName[LOG_FILENAME_LEN-1] = 0;
    }
    if (query.copyValue("log=", buf, sizeof(buf))) {
      startCmd = atoi(buf) != 0;
      stopCmd = !startCmd;
    }
    if (query.copyValue("restart=", buf, sizeof(buf))) {
      restartCmd = true;
      restartNew = (strcmp(buf, "new") == 0);
    }
  }

  if (restartCmd) {
    SDLogger::restartLogging(restartNew);
  } else if (startCmd) {
    SDLogger::startLogging(SDLogger::getLogMode(), restartNew);
  } else if (stopCmd) {
    SDLogger::stopLogging();
  }

  unoCfg.logEnabled = SDLogger::isLogging();
  unoCfg.logDaily   = (SDLogger::getLogMode() == SDLogger::LogMode::Daily);
  unoCfg.logAppend  = SDLogger::getAppendMode();
  strncpy(unoCfg.logBaseName, SDLogger::getFilename(), LOG_FILENAME_LEN);
  unoCfg.logBaseName[LOG_FILENAME_LEN-1] = 0;

  if (hasQuery) {
    applyUnoConfig(unoCfg);
    saveConfig(shared, unoCfg);
  }

  renderLoggingPage(response);
}

static void handleLogFilesRequest(HttpRequest& request, HttpResponse& response) {
  (void)request;
  response.setStatusCode(F("200 OK"));
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Connection", "close");
  response.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>Log Files</title></head><body>"));
  response.println(F("<h2>Log Files</h2>"));

  File root = SD.open("/");
  if (!root) {
    response.println(F("<p>SD not available.</p>"));
  } else {
    response.println(F("<ul>"));
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        char nameBuf[LOG_FILENAME_LEN] = {0};
        const char* nm = entry.name();
        if (nm) {
          strncpy(nameBuf, nm, sizeof(nameBuf)-1);
        }
        if (nameBuf[0] && SDLogger::isValidFilename(nameBuf)) {
          response.print(F("<li><a href='/download?file="));
          response.print(nameBuf);
          response.print(F("'>"));
          response.print(nameBuf);
          response.print(F("</a> ("));
          response.print((unsigned long)entry.size());
          response.println(F(" bytes)</li>"));
        }
      }
      File next = root.openNextFile();
      entry.close();
      entry = next;
    }
    response.println(F("</ul>"));
    root.close();
  }

  response.println(F("<p><a href='/log'>Back</a> | <a href='/' aria-label='Return to home page'>Home</a></p>"));
  response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
}

static void handleDownloadRequest(HttpRequest& request, HttpResponse& response) {
  String queryStr = request.query();
  QueryParams query(queryStr);
  char fname[LOG_FILENAME_LEN] = {0};
  if (!query.copyValue("file=", fname, sizeof(fname)) || !SDLogger::isValidFilename(fname)) {
    sendNotFound(response);
    return;
  }
  File f = SD.open(fname, FILE_READ);
  if (!f) {
    sendNotFound(response);
    return;
  }

  size_t fsize = f.size();
  response.setStatusCode(F("200 OK"));
  response.setHeader("Content-Type", "text/csv");
  response.setHeader("Content-Disposition", String("attachment; filename=\"") + fname + "\"");
  response.setHeader("Connection", "close");
  response.setHeader("Content-Length", String(fsize));
  response.beginBody(fsize);
  uint8_t buf[128];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n) response.write(buf, n);
  }
  f.close();
}

static void handleWiFiConfigRequest(HttpRequest& request, HttpResponse& response) {
  if (request.method() == HttpMethod::GET) {
    String queryStr = request.query();
    QueryParams query(queryStr);
    if (!query.empty()) {
      WiFiConfig::setProvisioning(query.flagEnabled("prov="));
    }
  }

  if (request.method() == HttpMethod::POST) {
    String body = request.body();
    QueryParams bodyParams(body);
    char ns[64] = {0};
    char np[64] = {0};
    bool connectNow = bodyParams.flagEnabled("connect=");
    bodyParams.copyValue("ssid=", ns, sizeof(ns));
    bodyParams.copyValue("pass=", np, sizeof(np));
    if (ns[0]) {
      WiFiStorage::saveCredentials(ns, np);
      WiFiConfig::setCredentials(ns, np, connectNow);
    }
    if (connectNow) {
      WiFiConfig::setProvisioning(false);
      WiFiConfig::requestReconnect();
    }
    response.setStatusCode(F("200 OK"));
    response.setHeader("Content-Type", "text/html");
    response.setHeader("Connection", "close");
    response.println(F("<html><head><meta charset='utf-8'><title>Saved</title></head><body>"));
    response.println(F("<h2>Credentials Saved</h2>"));
    if (connectNow) {
      response.println(F("<p>Attempting reconnect. If STA connects, the AP will shut down automatically.</p>"));
    } else if (WiFiConfig::isProvisioning()) {
      response.println(F("<p>Provisioning is enabled; STA reconnects are paused until provisioning is disabled.</p>"));
    } else {
      response.println(F("<p>Updated credentials will be used on the next reconnect attempt.</p>"));
    }
    response.println(F("<a href='/' aria-label='Return to home page'>Home</a>"));
    response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
    return;
  }

  response.setStatusCode(F("200 OK"));
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Connection", "close");
  response.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>WiFi Config</title></head><body>"));
  response.println(F("<h2>WiFi Configuration</h2>"));
  response.println(F("<form action='/wifi' method='post'>"));
  response.print(F("SSID: <input name='ssid' value='")); response.print(WiFiConfig::ssid()); response.println(F("'><br>"));
  response.println(F("Password: <input name='pass' type='password'><br><br>"));
  response.println(F("<button type='submit' name='connect' value='0'>Save (stay in AP)</button>"));
  response.println(F("<button type='submit' name='connect' value='1'>Save &amp; connect</button>"));
  response.println(F("</form>"));
  response.println(F("<a href='/' aria-label='Return to home page'>Home</a>"));
  response.println(F("<h3>Provisioning</h3>"));
  response.print(F("<p>Status: "));
  response.print(WiFiConfig::isProvisioning() ? F("ENABLED (AP forced on)") : F("DISABLED"));
  response.println(F("</p>"));
  response.println(F("<form action='/wifi' method='get'><input type='hidden' name='prov' value='1'><input type='submit' value='Enable provisioning'></form>"));
  response.println(F("<form action='/wifi' method='get'><input type='hidden' name='prov' value='0'><input type='submit' value='Disable provisioning &amp; reconnect'></form>"));
  response.println(F("<p><a href='/' aria-label='Return to home page'>Home</a></p>"));
  response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
}

static void handleUnoRequest(HttpRequest& request, HttpResponse& response) {
  String queryStr = request.query();
  QueryParams query(queryStr);
  bool save = !query.empty();
  TunableConfig shared = getCurrentConfig();
  UnoConfig unoCfg = getCurrentUnoConfig();
  if (save) {
    char val[32];
    if (query.copyValue("statsWindowSize=", val, sizeof(val))) unoCfg.statsWindowSize = atoi(val);
    if (query.copyValue("rollingWindowMs=", val, sizeof(val))) unoCfg.rollingWindowMs = atoi(val);
    if (query.copyValue("blockJumpUs=", val, sizeof(val)))    unoCfg.blockJumpUs = atoi(val);
    if (query.copyValue("dataUnits=", val, sizeof(val)))      shared.dataUnits = atoi(val);

    bool logParam=false; int logVal=0;
    if (query.copyValue("log=", val, sizeof(val))) { logParam=true; logVal=atoi(val); unoCfg.logEnabled = logVal; }
    if (query.copyValue("logDaily=", val, sizeof(val))) unoCfg.logDaily = atoi(val);
    if (query.copyValue("append=", val, sizeof(val))) { unoCfg.logAppend = atoi(val); SDLogger::setAppendMode(unoCfg.logAppend); }
    if (query.copyValue("file=", val, sizeof(val))) { SDLogger::setFilename(val); strncpy(unoCfg.logBaseName, SDLogger::getFilename(), LOG_FILENAME_LEN); unoCfg.logBaseName[LOG_FILENAME_LEN-1] = 0; }
    SDLogger::setLogMode(unoCfg.logDaily ? SDLogger::LogMode::Daily : SDLogger::LogMode::Continuous);
    applyUnoConfig(unoCfg);
    applyConfig(shared);
    StatsEngine::reset();

    if (logParam) {
      if (logVal) SDLogger::startLogging(SDLogger::getLogMode(), false);
      else SDLogger::stopLogging();
    }

    unoCfg.logEnabled = SDLogger::isLogging();
    unoCfg.logDaily   = (SDLogger::getLogMode() == SDLogger::LogMode::Daily);
    unoCfg.logAppend  = SDLogger::getAppendMode();
    strncpy(unoCfg.logBaseName, SDLogger::getFilename(), LOG_FILENAME_LEN);
    unoCfg.logBaseName[LOG_FILENAME_LEN-1] = 0;
    saveConfig(shared, unoCfg);
    response.setStatusCode(F("200 OK"));
    response.setHeader("Content-Type", "text/html");
    response.setHeader("Connection", "close");
    response.println(F("<html><head><meta charset='utf-8'><title>Saved</title></head><body>"));
    response.println(F("<h2>UNO Config Saved</h2><a href='/uno'>Back</a>"));
    response.println(F("<a href='/' aria-label='Return to home page'>Home</a>"));
    response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
    return;
  }

  unoCfg.logEnabled = SDLogger::isLogging();
  unoCfg.logDaily   = (SDLogger::getLogMode() == SDLogger::LogMode::Daily);
  unoCfg.logAppend  = SDLogger::getAppendMode();
  strncpy(unoCfg.logBaseName, SDLogger::getFilename(), LOG_FILENAME_LEN);
  unoCfg.logBaseName[LOG_FILENAME_LEN-1] = 0;

  response.setStatusCode(F("200 OK"));
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Connection", "close");
  response.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>UNO Tunables</title></head><body>"));
  response.println(F("<h2>UNO Tunables</h2>"));
  response.println(F("<form action='/uno' method='get'>"));
  response.print(F("statsWindowSize: <input name='statsWindowSize' value='")); response.print(unoCfg.statsWindowSize); response.println(F("'><br>"));
  response.print(F("rollingWindowMs: <input name='rollingWindowMs' value='")); response.print(unoCfg.rollingWindowMs); response.println(F("'><br>"));
  response.print(F("blockJumpUs: <input name='blockJumpUs' value='")); response.print(unoCfg.blockJumpUs); response.println(F("'><br>"));
  response.print(F("dataUnits: <input name='dataUnits' value='")); response.print(shared.dataUnits); response.println(F("'><br>"));

  response.print(F("log: <input name='log' value='")); response.print(unoCfg.logEnabled ? 1 : 0); response.println(F("'><br>"));
  response.print(F("logDaily: <input name='logDaily' value='")); response.print(unoCfg.logDaily ? 1 : 0); response.println(F("'><br>"));
  response.print(F("append: <input name='append' value='")); response.print(unoCfg.logAppend ? 1 : 0); response.println(F("'><br>"));
  response.print(F("file: <input name='file' value='")); response.print(unoCfg.logBaseName); response.println(F("'><br>"));
  response.println(F("<input type='submit' value='Save'></form>"));
  response.println(F("<p>Configure WiFi on the <a href='/wifi'>WiFi page</a>.</p>"));
  response.println(F("<p><a href='/log'>Logging controls</a></p>"));
  response.println(F("<a href='/' aria-label='Return to home page'>Home</a>"));
  response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
}

static bool nanoCommand(const char* cmd, char* resp, size_t respLen) {
  NANO_SERIAL.println(cmd);
  unsigned long start = millis();
  while (millis() - start < 200) {
    if (NANO_SERIAL.available()) {
      size_t n = NANO_SERIAL.readBytesUntil('\n', resp, respLen-1);
      resp[n] = '\0';
      while (n > 0 && (resp[n-1] == '\r' || resp[n-1] == ' ')) resp[--n] = '\0';
      return true;
    }
  }
  return false;
}

static bool nanoGet(const char* param, char* val, size_t valLen) {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "get %s", param);
  char resp[64];
  if (!nanoCommand(cmd, resp, sizeof(resp))) return false;
  char* eq = strchr(resp, '=');
  if (eq) {
    strncpy(val, eq+1, valLen-1);
    val[valLen-1] = '\0';
    urlDecode(val);
    return true;
  }
  return false;
}

static void nanoSet(const char* param, const char* val) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "set %s %s", param, val);
  char resp[8];
  nanoCommand(cmd, resp, sizeof(resp));
}

static void handleNanoRequest(HttpRequest& request, HttpResponse& response) {
  String queryStr = request.query();
  QueryParams query(queryStr);
  bool save = !query.empty();
  char val[32];
  if (save) {
    if (query.copyValue("correctionJumpThresh=", val, sizeof(val))) nanoSet(PARAM_CORR_JUMP, val);
    if (query.copyValue("ppsEmaShift=", val, sizeof(val)))          nanoSet(PARAM_PPS_EMA_SHIFT, val);
    if (query.copyValue("dataUnits=", val, sizeof(val)))            nanoSet(PARAM_DATA_UNITS, val);
    if (query.copyValue("ppsFastShift=", val, sizeof(val)))         nanoSet(PARAM_PPS_FAST_SHIFT, val);
    if (query.copyValue("ppsSlowShift=", val, sizeof(val)))         nanoSet(PARAM_PPS_SLOW_SHIFT, val);
    if (query.copyValue("ppsHampelWin=", val, sizeof(val)))         nanoSet(PARAM_PPS_HAMPEL_WIN, val);
    if (query.copyValue("ppsHampelKx100=", val, sizeof(val)))       nanoSet(PARAM_PPS_HAMPEL_KX100, val);
    if (query.copyValue("ppsMedian3=", val, sizeof(val)))           nanoSet(PARAM_PPS_MEDIAN3, val);
    if (query.copyValue("ppsBlendLoPpm=", val, sizeof(val)))        nanoSet(PARAM_PPS_BLEND_LO_PPM, val);
    if (query.copyValue("ppsBlendHiPpm=", val, sizeof(val)))        nanoSet(PARAM_PPS_BLEND_HI_PPM, val);
    if (query.copyValue("ppsLockRppm=", val, sizeof(val)))          nanoSet(PARAM_PPS_LOCK_R_PPM, val);
    if (query.copyValue("ppsLockJppm=", val, sizeof(val)))          nanoSet(PARAM_PPS_LOCK_J_PPM, val);
    if (query.copyValue("ppsUnlockRppm=", val, sizeof(val)))        nanoSet(PARAM_PPS_UNLOCK_R_PPM, val);
    if (query.copyValue("ppsUnlockJppm=", val, sizeof(val)))        nanoSet(PARAM_PPS_UNLOCK_J_PPM, val);
    if (query.copyValue("ppsUnlockCount=", val, sizeof(val)))       nanoSet(PARAM_PPS_UNLOCK_COUNT, val);
    if (query.copyValue("ppsHoldoverMs=", val, sizeof(val)))        nanoSet(PARAM_PPS_HOLDOVER_MS, val);
    response.setStatusCode(F("200 OK"));
    response.setHeader("Content-Type", "text/html");
    response.setHeader("Connection", "close");
    response.println(F("<html><head><meta charset='utf-8'><title>Saved</title></head><body>"));
    response.println(F("<h2>Nano Tunables Saved</h2><a href='/nano'>Back</a>"));
    response.println(F("<a href='/' aria-label='Return to home page'>Home</a>"));
    response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
    return;
  }

  float correctionJump = 0.0f;
  int ppsEmaShift = 0;
  int dataUnitsVal = 0;
  int ppsFastShift = 0;
  int ppsSlowShift = 0;
  int ppsHampelWin = 0;
  int ppsHampelKx100 = 0;
  int ppsMedian3 = 0;
  int ppsBlendLoPpm = 0;
  int ppsBlendHiPpm = 0;
  int ppsLockRppm = 0;
  int ppsLockJppm = 0;
  int ppsUnlockRppm = 0;
  int ppsUnlockJppm = 0;
  int ppsUnlockCount = 0;
  int ppsHoldoverMs = 0;

  if (nanoGet(PARAM_CORR_JUMP, val, sizeof(val)))       correctionJump = atof(val);
  if (nanoGet(PARAM_PPS_EMA_SHIFT, val, sizeof(val)))   ppsEmaShift = atoi(val);
  if (nanoGet(PARAM_DATA_UNITS, val, sizeof(val)))      dataUnitsVal = atoi(val);
  if (nanoGet(PARAM_PPS_FAST_SHIFT, val, sizeof(val)))  ppsFastShift = atoi(val);
  if (nanoGet(PARAM_PPS_SLOW_SHIFT, val, sizeof(val)))  ppsSlowShift = atoi(val);
  if (nanoGet(PARAM_PPS_HAMPEL_WIN, val, sizeof(val)))  ppsHampelWin = atoi(val);
  if (nanoGet(PARAM_PPS_HAMPEL_KX100, val, sizeof(val))) ppsHampelKx100 = atoi(val);
  if (nanoGet(PARAM_PPS_MEDIAN3, val, sizeof(val)))     ppsMedian3 = atoi(val);
  if (nanoGet(PARAM_PPS_BLEND_LO_PPM, val, sizeof(val))) ppsBlendLoPpm = atoi(val);
  if (nanoGet(PARAM_PPS_BLEND_HI_PPM, val, sizeof(val))) ppsBlendHiPpm = atoi(val);
  if (nanoGet(PARAM_PPS_LOCK_R_PPM, val, sizeof(val)))  ppsLockRppm = atoi(val);
  if (nanoGet(PARAM_PPS_LOCK_J_PPM, val, sizeof(val)))  ppsLockJppm = atoi(val);
  if (nanoGet(PARAM_PPS_UNLOCK_R_PPM, val, sizeof(val))) ppsUnlockRppm = atoi(val);
  if (nanoGet(PARAM_PPS_UNLOCK_J_PPM, val, sizeof(val))) ppsUnlockJppm = atoi(val);
  if (nanoGet(PARAM_PPS_UNLOCK_COUNT, val, sizeof(val))) ppsUnlockCount = atoi(val);
  if (nanoGet(PARAM_PPS_HOLDOVER_MS, val, sizeof(val)))  ppsHoldoverMs = atoi(val);

  response.setStatusCode(F("200 OK"));
  response.setHeader("Content-Type", "text/html");
  response.setHeader("Connection", "close");
  response.println(F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>Nano Tunables</title></head><body>"));
  response.println(F("<h2>Nano Tunables</h2>"));
  response.println(F("<form action='/nano' method='get'>"));
  response.print(F("correctionJumpThresh: <input name='correctionJumpThresh' value='")); response.print(correctionJump); response.println(F("'><br>"));
  response.print(F("ppsEmaShift: <input name='ppsEmaShift' value='")); response.print(ppsEmaShift); response.println(F("'><br>"));
  response.print(F("dataUnits: <input name='dataUnits' value='")); response.print(dataUnitsVal); response.println(F("'><br>"));
  response.print(F("ppsFastShift: <input name='ppsFastShift' value='")); response.print(ppsFastShift); response.println(F("'><br>"));
  response.print(F("ppsSlowShift: <input name='ppsSlowShift' value='")); response.print(ppsSlowShift); response.println(F("'><br>"));
  response.print(F("ppsHampelWin: <input name='ppsHampelWin' value='")); response.print(ppsHampelWin); response.println(F("'><br>"));
  response.print(F("ppsHampelKx100: <input name='ppsHampelKx100' value='")); response.print(ppsHampelKx100); response.println(F("'><br>"));
  response.print(F("ppsMedian3: <input name='ppsMedian3' value='")); response.print(ppsMedian3); response.println(F("'><br>"));
  response.print(F("ppsBlendLoPpm: <input name='ppsBlendLoPpm' value='")); response.print(ppsBlendLoPpm); response.println(F("'><br>"));
  response.print(F("ppsBlendHiPpm: <input name='ppsBlendHiPpm' value='")); response.print(ppsBlendHiPpm); response.println(F("'><br>"));
  response.print(F("ppsLockRppm: <input name='ppsLockRppm' value='")); response.print(ppsLockRppm); response.println(F("'><br>"));
  response.print(F("ppsLockJppm: <input name='ppsLockJppm' value='")); response.print(ppsLockJppm); response.println(F("'><br>"));
  response.print(F("ppsUnlockRppm: <input name='ppsUnlockRppm' value='")); response.print(ppsUnlockRppm); response.println(F("'><br>"));
  response.print(F("ppsUnlockJppm: <input name='ppsUnlockJppm' value='")); response.print(ppsUnlockJppm); response.println(F("'><br>"));
  response.print(F("ppsUnlockCount: <input name='ppsUnlockCount' value='")); response.print(ppsUnlockCount); response.println(F("'><br>"));
  response.print(F("ppsHoldoverMs: <input name='ppsHoldoverMs' value='")); response.print(ppsHoldoverMs); response.println(F("'><br>"));

  response.println(F("<input type='submit' value='Save'></form>"));
  response.println(F("<a href='/' aria-label='Return to home page'>Home</a>"));
  response.println(F("<hr><small>UNO R4 Pendulum Logger</small></body></html>"));
}

static void handleStatsRequest(HttpRequest& request, HttpResponse& response) {
  (void)request;
  sendProgmemHtml(response, STATS_PAGE);
}

static void handleUnknown(HttpRequest& request, HttpResponse& response) {
  (void)request;
  sendNotFound(response);
}

  void begin() {
    httpServer.on(Method::GET, "/", handleHomePage);
    httpServer.on(Method::GET, "/json", handleJsonRequest);
    httpServer.on(Method::GET, "/wifi", handleWiFiConfigRequest);
    httpServer.on(Method::POST, "/wifi", handleWiFiConfigRequest);
    httpServer.on(Method::GET, "/uno", handleUnoRequest);
    httpServer.on(Method::GET, "/nano", handleNanoRequest);
    httpServer.on(Method::GET, "/stats", handleStatsRequest);
    httpServer.on(Method::GET, "/stats.json", handleStatsJsonRequest);
    httpServer.on(Method::GET, "/log", handleLogRequest);
    httpServer.on(Method::GET, "/logfiles", handleLogFilesRequest);
    httpServer.on(Method::GET, "/download", handleDownloadRequest);
    httpServer.onNotFound(handleUnknown);
    httpServer.begin();
  }

  void service() {
    httpServer.poll();
  }

} // namespace HttpServer
