#include "net.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
// Le header Arduino (WiFiClientSecure/esp_crt_bundle.h) masque celui d'ESP-IDF : on declare le symbole IDF,
// present dans libmbedtls.a avec le bundle de CA Mozilla complet (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL).
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);
#include <esp_heap_caps.h>
#include <time.h>

namespace net {

static bool sBegun = false;  // association deja lancee (wifiBegin)

static bool beginFast(const Settings &s) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  bool fast = rtc.channel != 0;  // canal + BSSID memorises en RTC (SPEC 12.2)
  if (fast)
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str(), rtc.channel, rtc.bssid, true);
  else
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
  sBegun = true;
  return fast;
}

void wifiBegin(const Settings &s) {
  if (!sBegun && WiFi.status() != WL_CONNECTED) beginFast(s);
}

bool wifiConnect(const Settings &s, uint32_t timeoutMs) {
  bool fast = sBegun ? false : beginFast(s);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(50);
    // Repli : si la connexion rapide echoue a mi-parcours, retente un scan complet.
    if (fast && millis() - t0 > timeoutMs / 2 && WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
      fast = false;
      t0 = millis() - timeoutMs / 2;
    }
  }
  sBegun = false;
  if (WiFi.status() != WL_CONNECTED) {
    rtc.channel = 0;  // invalide le cache
    return false;
  }
  rtc.channel = WiFi.channel();
  memcpy(rtc.bssid, WiFi.BSSID(), 6);
  return true;
}

void wifiOff() {
  sBegun = false;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

std::string currentSsid() { return WiFi.status() == WL_CONNECTED ? std::string(WiFi.SSID().c_str()) : std::string(); }

bool ensureTime(uint32_t timeoutMs) {
  if (time(nullptr) > 1700000000) return true;
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  uint32_t t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < timeoutMs) delay(100);
  return time(nullptr) > 1700000000;
}

Buf::~Buf() {
  if (p) heap_caps_free(p);
}

bool Buf::append(const uint8_t *d, size_t n) {
  if (maxLen && len + n > maxLen) { overflow = true; return false; }
  if (len + n + 1 > cap) {
    size_t nc = (len + n + 1) * 3 / 2 + 256;
    uint8_t *np = (uint8_t *)heap_caps_realloc(p, nc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!np) np = (uint8_t *)heap_caps_realloc(p, nc, MALLOC_CAP_8BIT);
    if (!np) return false;
    p = np; cap = nc;
  }
  memcpy(p + len, d, n);
  len += n;
  p[len] = 0;
  return true;
}

static esp_err_t onEvent(esp_http_client_event_t *e) {
  if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data && e->data_len > 0) {
    ((Buf *)e->user_data)->append((const uint8_t *)e->data, e->data_len);
  }
  return ESP_OK;
}

void request(Resp &r, esp_http_client_method_t method, const std::string &url, const std::string &token,
             const uint8_t *body, size_t blen, const char *ctype) {
  esp_http_client_config_t c = {};
  c.url = url.c_str();
  c.method = method;
  c.timeout_ms = cfg::kHttpTimeoutMs;
  c.event_handler = onEvent;
  c.user_data = &r.body;
  c.buffer_size = 2048;
  c.buffer_size_tx = 1536;
  c.disable_auto_redirect = true;
  // TODO: reprise de session TLS entre deux reveils (ticket en RTC) - non expose par esp_http_client.
  if (url.rfind("https://", 0) == 0) c.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) return;
  if (!token.empty()) {
    std::string a = "Bearer " + token;
    esp_http_client_set_header(h, "Authorization", a.c_str());
  }
  esp_http_client_set_header(h, "Accept", "application/json, audio/wav, */*");
  if (ctype) esp_http_client_set_header(h, "Content-Type", ctype);
  if (body && blen) esp_http_client_set_post_field(h, (const char *)body, blen);
  esp_err_t err = esp_http_client_perform(h);
  if (err != ESP_OK && method == HTTP_METHOD_GET && !r.body.overflow) {
    // Requete idempotente : une seule nouvelle tentative apres une erreur de transport (connexion coupee, timeout).
    log_w("HTTP %s -> %s, nouvel essai", url.c_str(), esp_err_to_name(err));
    r.body.len = 0;
    delay(300);
    err = esp_http_client_perform(h);
  }
  if (err == ESP_OK) r.code = esp_http_client_get_status_code(h);
  else log_w("HTTP %s -> %s", url.c_str(), esp_err_to_name(err));
  if (r.body.overflow) r.code = -1;
  esp_http_client_cleanup(h);
}

}  // namespace net
