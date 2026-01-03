#include "ArduinoHttpServer.h"
#include <ctype.h>
#include <string.h>
#include <avr/pgmspace.h>

namespace ArduinoHttpServer {

bool parseRequest(WiFiClient& client, Request& request) {
  String requestLine = client.readStringUntil('\n');
  requestLine.trim();
  if (requestLine.length() == 0) return false;

  int firstSpace = requestLine.indexOf(' ');
  int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) return false;

  String method = requestLine.substring(0, firstSpace);
  if (method == F("GET")) request.method_ = Method::GET;
  else if (method == F("POST")) request.method_ = Method::POST;
  else request.method_ = Method::UNKNOWN;

  request.rawUrl_ = requestLine.substring(firstSpace + 1, secondSpace);
  int qPos = request.rawUrl_.indexOf('?');
  if (qPos >= 0) {
    request.path_ = request.rawUrl_.substring(0, qPos);
    request.query_ = request.rawUrl_.substring(qPos + 1);
  } else {
    request.path_ = request.rawUrl_;
    request.query_ = String();
  }

  size_t contentLength = 0;
  while (true) {
    String headerLine = client.readStringUntil('\n');
    headerLine.trim();
    if (headerLine.length() == 0) break;
    int colon = headerLine.indexOf(':');
    if (colon < 0) continue;
    String key = headerLine.substring(0, colon);
    String value = headerLine.substring(colon + 1);
    key.trim();
    value.trim();
    key.toLowerCase();
    if (key == F("content-length")) {
      contentLength = (size_t)value.toInt();
    }
  }

  if (contentLength > 0) {
    request.body_.reserve(contentLength);
    while (request.body_.length() < contentLength && client.connected()) {
      if (client.available()) {
        request.body_ += char(client.read());
      }
    }
  }

  return true;
}

void Response::setStatusCode(int statusCode) {
  statusCode_ = String(statusCode);
  switch (statusCode) {
    case 200: statusCode_ += F(" OK"); break;
    case 404: statusCode_ += F(" Not Found"); break;
    default: break;
  }
}

void Response::setHeader(const String& key, const String& value) {
  if (headerCount_ >= MAX_HEADERS) return;
  headers_[headerCount_][0] = key;
  headers_[headerCount_][1] = value;
  if (key.equalsIgnoreCase(F("Content-Length"))) {
    contentLengthSet_ = true;
  } else if (key.equalsIgnoreCase(F("Connection"))) {
    connectionSet_ = true;
  }
  headerCount_++;
}

void Response::sendHeaders() {
  if (headersSent_) return;
  headersSent_ = true;

  if (!connectionSet_) {
    setHeader(F("Connection"), F("close"));
  }

  client_.print(F("HTTP/1.1 "));
  client_.println(statusCode_);
  for (uint8_t i = 0; i < headerCount_; i++) {
    client_.print(headers_[i][0]);
    client_.print(F(": "));
    client_.println(headers_[i][1]);
  }
  client_.println();
}

void Response::beginBody(size_t length) {
  if (length > 0 && !contentLengthSet_) {
    setHeader(F("Content-Length"), String(length));
  }
  sendHeaders();
}

size_t Response::write(const uint8_t* data, size_t len) {
  sendHeaders();
  return client_.write(data, len);
}

size_t Response::write_P(PGM_P data, size_t len) {
  sendHeaders();
  size_t written = 0;
  for (size_t i = 0; i < len; i++) {
    client_.write(pgm_read_byte(data + i));
    written++;
  }
  return written;
}

void Server::begin() {
  server_.begin();
}

void Server::on(Method method, const char* path, Handler handler) {
  if (routeCount_ >= MAX_ROUTES) return;
  routes_[routeCount_++] = {method, path, handler};
}

void Server::poll() {
  WiFiClient client = server_.available();
  if (!client) return;

  unsigned long startTime = millis();
  while (client.connected() && !client.available() && (millis() - startTime) < HTTP_IDLE_TIMEOUT_MS) {}
  if (!client.available()) {
    client.stop();
    return;
  }

  Request request;
  if (!parseRequest(client, request)) {
    client.stop();
    return;
  }

  Response response(client);

  Handler handler = nullptr;
  for (uint8_t i = 0; i < routeCount_; i++) {
    if (routes_[i].method == request.method() && request.path() == routes_[i].path) {
      handler = routes_[i].handler;
      break;
    }
  }

  if (handler) {
    handler(request, response);
  } else if (notFoundHandler_) {
    notFoundHandler_(request, response);
  }

  client.flush();
  client.stop();
}

} // namespace ArduinoHttpServer
