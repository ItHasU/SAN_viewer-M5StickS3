#include "portal.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "ui.h"

bool runConfigPortal(Settings &s) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char ap[24];
  snprintf(ap, sizeof ap, "SAN-Viewer-%02X%02X", mac[4], mac[5]);
  ui::backlightOn();
  ui::message("Configuration", std::string("Connectez-vous au Wi-Fi\n") + ap + "\npuis ouvrez\n192.168.4.1", TFT_YELLOW);

  WiFiManager wm;
  WiFiManagerParameter pSrv("srv", "URL du serveur (https://...)", s.serverUrl.c_str(), 160);
  WiFiManagerParameter pLan("lan", "URL LAN (optionnel, http://nas:3000)", s.lanUrl.c_str(), 160);
  WiFiManagerParameter pHome("home", "SSID domestique (pour l'URL LAN)", s.homeSsid.c_str(), 40);
  wm.addParameter(&pSrv);
  wm.addParameter(&pLan);
  wm.addParameter(&pHome);
  wm.setConfigPortalTimeout(cfg::kPortalTimeoutS);
  wm.setBreakAfterConfig(true);
  wm.setTitle("SAN Viewer");
  bool saved = false;
  wm.setSaveParamsCallback([&]() { saved = true; });

  WiFi.persistent(false);
  bool ok = wm.startConfigPortal(ap);
  if (!ok && !saved) { WiFi.mode(WIFI_OFF); return false; }
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
  return s.configured();
}
