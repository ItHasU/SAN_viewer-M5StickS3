// Wi-Fi + HTTP(S) (esp_http_client, bundle de CA Mozilla integre a Arduino-ESP32).
#pragma once
#include <Arduino.h>
#include <esp_http_client.h>

#include <string>

#include "hw_state.h"

namespace net {

bool wifiConnect(const Settings &s, uint32_t timeoutMs);
void wifiOff();
std::string currentSsid();
// TLS valide les dates des certificats : synchronise l'heure par SNTP si elle est invalide.
bool ensureTime(uint32_t timeoutMs = 6000);

struct Buf {
  uint8_t *p = nullptr;
  size_t len = 0, cap = 0, maxLen = 0;
  bool overflow = false;
  explicit Buf(size_t max = 0) : maxLen(max) {}
  ~Buf();
  Buf(const Buf &) = delete;
  Buf &operator=(const Buf &) = delete;
  bool append(const uint8_t *d, size_t n);
  std::string str() const { return std::string((const char *)p, len); }
};

struct Resp {
  int code = -1;  // -1 : erreur transport
  Buf body;
  explicit Resp(size_t max) : body(max) {}
  std::string text() const { return body.str(); }
  bool ok() const { return code >= 200 && code < 300; }
};

// body/ctype facultatifs. token vide : pas d'en-tete Authorization.
void request(Resp &r, esp_http_client_method_t method, const std::string &url, const std::string &token,
             const uint8_t *body = nullptr, size_t blen = 0, const char *ctype = nullptr);

}  // namespace net
