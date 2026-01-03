#pragma once
#include <WiFiS3.h>
#include <Arduino.h>

namespace ArduinoHttpServer {

enum class Method { GET, POST, UNKNOWN };

class Request {
public:
  Method method() const { return method_; }
  const String& path() const { return path_; }
  const String& query() const { return query_; }
  const String& body() const { return body_; }
  const String& rawUrl() const { return rawUrl_; }

private:
  Method method_ = Method::UNKNOWN;
  String path_;
  String query_;
  String body_;
  String rawUrl_;

  friend bool parseRequest(WiFiClient& client, Request& request);
};

class Response {
public:
  explicit Response(WiFiClient& client) : client_(client) {}

  void setStatusCode(int statusCode);
  void setStatusCode(const __FlashStringHelper* status) { statusCode_ = String(status); }
  void setStatusCode(const String& status) { statusCode_ = status; }

  void setHeader(const __FlashStringHelper* key, const __FlashStringHelper* value) {
    setHeader(String(key), String(value));
  }
  void setHeader(const char* key, const char* value) { setHeader(String(key), String(value)); }
  void setHeader(const String& key, const String& value);

  void beginBody(size_t length = 0);
  size_t write(const uint8_t* data, size_t len);
  size_t write_P(PGM_P data, size_t len);

  void print(const __FlashStringHelper* val) { sendHeaders(); client_.print(val); }
  void print(const String& val) { sendHeaders(); client_.print(val); }
  void print(const char* val) { sendHeaders(); client_.print(val); }
  void print(int val) { sendHeaders(); client_.print(val); }
  void print(unsigned int val) { sendHeaders(); client_.print(val); }
  void print(long val) { sendHeaders(); client_.print(val); }
  void print(unsigned long val) { sendHeaders(); client_.print(val); }
  void print(float val) { sendHeaders(); client_.print(val); }
  void print(double val) { sendHeaders(); client_.print(val); }

  void println(const __FlashStringHelper* val) { sendHeaders(); client_.println(val); }
  void println(const String& val) { sendHeaders(); client_.println(val); }
  void println(const char* val) { sendHeaders(); client_.println(val); }
  void println(int val) { sendHeaders(); client_.println(val); }
  void println(unsigned int val) { sendHeaders(); client_.println(val); }
  void println(long val) { sendHeaders(); client_.println(val); }
  void println(unsigned long val) { sendHeaders(); client_.println(val); }
  void println(float val) { sendHeaders(); client_.println(val); }
  void println(double val) { sendHeaders(); client_.println(val); }

private:
  static constexpr uint8_t MAX_HEADERS = 10;

  void sendHeaders();

  WiFiClient& client_;
  String statusCode_ = F("200 OK");
  String headers_[MAX_HEADERS][2];
  uint8_t headerCount_ = 0;
  bool headersSent_ = false;
  bool contentLengthSet_ = false;
  bool connectionSet_ = false;
};

using Handler = void (*)(Request&, Response&);

class Server {
public:
  explicit Server(WiFiServer& server) : server_(server) {}
  void begin();
  void poll();
  void on(Method method, const char* path, Handler handler);
  void onNotFound(Handler handler) { notFoundHandler_ = handler; }

private:
  static constexpr uint8_t MAX_ROUTES = 12;
  static constexpr uint32_t HTTP_IDLE_TIMEOUT_MS = 2500;

  struct Route { Method method; const char* path; Handler handler; };
  WiFiServer& server_;
  Route routes_[MAX_ROUTES];
  uint8_t routeCount_ = 0;
  Handler notFoundHandler_ = nullptr;
};

} // namespace ArduinoHttpServer
