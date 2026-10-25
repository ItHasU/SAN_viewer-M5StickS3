#include "portal.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "ui.h"

PortalResult runConfigPortal(Settings &s, bool cancelable) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char ap[24];
  snprintf(ap, sizeof ap, "SAN-Viewer-%02X%02X", mac[4], mac[5]);
  ui::backlightOn();
  ui::portal(ap, 100);

  WiFiManager wm;
  WiFiManagerParameter pSrv("srv", "URL du serveur (https://...)", s.serverUrl.c_str(), 160);
  WiFiManagerParameter pLan("lan", "URL LAN (optionnel, http://nas:3000)", s.lanUrl.c_str(), 160);
  WiFiManagerParameter pHome("home", "SSID domestique (pour l'URL LAN)", s.homeSsid.c_str(), 40);
  wm.addParameter(&pSrv);
  wm.addParameter(&pLan);
  wm.addParameter(&pHome);
  wm.setConfigPortalTimeout(cfg::kPortalTimeoutS);
  wm.setConfigPortalBlocking(false);  // on garde la main pour les boutons et la barre de temps
  wm.setBreakAfterConfig(true);
  wm.setTitle("SAN Viewer");
  bool saved = false;
  wm.setSaveParamsCallback([&]() { saved = true; });

  WiFi.persistent(false);
  wm.startConfigPortal(ap);
  uint32_t t0 = millis(), lastBar = 0;
  bool ok = false, cancelled = false;
  while (wm.getConfigPortalActive()) {
    M5.update();
    if (cancelable && (M5.BtnA.wasClicked() || M5.BtnB.wasClicked())) { cancelled = true; break; }
    if (wm.process()) { ok = true; break; }
    if (!saved && millis() - lastBar > 1000) {
      lastBar = millis();
      uint32_t el = millis() - t0, tot = (uint32_t)cfg::kPortalTimeoutS * 1000u;
      ui::portal(ap, el >= tot ? 0 : (int)((tot - el) * 100 / tot));
    }
    delay(5);
  }
  if (cancelled) wm.stopConfigPortal();
  if (cancelled || (!ok && !saved)) {
    WiFi.mode(WIFI_OFF);
    Serial.printf("[san] portail %s\n", cancelled ? "annule" : "sans configuration (timeout)");
    return cancelled ? PortalResult::Cancelled : PortalResult::Timeout;
  }
  s.serverUrl = pSrv.getValue();
  s.lanUrl = pLan.getValue();
  s.homeSsid = pHome.getValue();
  s.serverUrl.trim(); s.lanUrl.trim(); s.homeSsid.trim();
  if (ok) {
    s.wifiSsid = WiFi.SSID();
    s.wifiPass = WiFi.psk();
  }
  if (s.homeSsid.isEmpty()) s.homeSsid = s.wifiSsid;
  WiFi.mode(WIFI_OFF);
  settingsSave(s);
  return s.configured() ? PortalResult::Saved : PortalResult::Timeout;
}
