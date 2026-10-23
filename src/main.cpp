// Firmware viewer SAN - M5StickS3. Cycle : reveil -> Wi-Fi -> /viewer/sync -> nudges/clips -> deep sleep.
// Toute la logique de decision est dans lib/san_core (testee sur hote).
#include <M5Unified.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include "audio_io.h"
#include "config.h"
#include "hw_state.h"
#include "net.h"
#include "portal.h"
#include "san_audio.h"
#include "san_logic.h"
#include "san_protocol.h"
#include "ui.h"

using namespace san;

// ---------------- etat d'execution ----------------
static Settings S;
static Config gCfg;
static std::vector<AgentState> gStates;  // etats bruts (cache RTC ou derniere sync)
static std::string gBase;
static int gBattery = -1;
static bool gButtonWake = false;
static bool gScreenOn = false;
static bool gOnline = false;
static uint32_t gActivity = 0;
static int gNextSyncS = 0;
static Phase gPhase = Phase::Boot;

static void touch() { gActivity = millis(); }
static void screenOn() {
  if (!gScreenOn) { ui::backlightOn(); gScreenOn = true; }
  touch();
}
static std::vector<AgentState> view() { return orderStates(gCfg, gStates); }
static void drawStatus(size_t sel) { ui::status(view(), gCfg, sel, gBattery); }

// ---------------- reseau / API ----------------
static bool ensureOnline() {
  if (gOnline && WiFi.status() == WL_CONNECTED) return true;
  gOnline = net::wifiConnect(S, cfg::kWifiTimeoutMs);
  if (gOnline) {
    gBase = chooseBase(S.serverUrl.c_str(), S.lanUrl.c_str(), net::currentSsid(), S.homeSsid.c_str());
    if (gBase.rfind("https://", 0) == 0) net::ensureTime();
  }
  return gOnline;
}

// 401 : efface le jeton et repart de REGISTER (SPEC 3.3 / 12.1). Renvoie true si la phase a change.
static bool handleAuth(int code) {
  Phase np = phaseOnHttp(gPhase, code);
  if (httpCodeClearsToken(code)) {
    tokenClear(S);
    uint8_t ch = rtc.channel; uint8_t bs[6]; memcpy(bs, rtc.bssid, 6);
    rtcReset();  // ack, hashes, config : tout est lie a l'ancien jeton
    rtc.channel = ch; memcpy(rtc.bssid, bs, 6);
    gCfg = Config(); gStates.clear();
  }
  bool changed = np != gPhase;
  gPhase = np;
  if (changed && np == Phase::Pairing) rtc.active = 0;
  return changed;
}

static void call(net::Resp &r, esp_http_client_method_t m, const std::string &url, const uint8_t *body = nullptr,
                 size_t blen = 0, const char *ctype = nullptr) {
  net::request(r, m, url, S.token.c_str(), body, blen, ctype);
}
static void callJson(net::Resp &r, esp_http_client_method_t m, const std::string &url, const std::string &body) {
  call(r, m, url, (const uint8_t *)body.data(), body.size(), "application/json");
}

static void adoptConfig(const Config &c, const std::string &hash) {
  gCfg = c;
  rtcStoreConfig(c);
  if (!hash.empty()) strlcpy(rtc.configHash, hash.c_str(), sizeof rtc.configHash);
}

static bool putCapabilities() {
  std::string body = buildCapabilitiesJson(cfg::kFirmware);
  net::Resp r(4096);
  callJson(r, HTTP_METHOD_PUT, capabilitiesUrl(gBase), body);
  if (handleAuth(r.code)) return false;
  if (!r.ok()) return false;
  strlcpy(rtc.capsHash, capsHash(body).c_str(), sizeof rtc.capsHash);
  return true;
}

// ---------------- evenements ----------------
static void waitOrButton(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    M5.update();
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) break;
    delay(20);
  }
}

static bool downloadAndPlay(const Clip &clip, const std::string &agent) {
  net::Resp r(cfg::kMaxClipBytes);
  call(r, HTTP_METHOD_GET, voiceClipUrl(gBase, clip.id));
  if (handleAuth(r.code)) return false;
  if (r.code == 404 || r.code == 410) return true;  // clip purge : inutile de reessayer
  if (!r.ok()) return false;
  strlcpy(rtc.lastClipId, clip.id.c_str(), sizeof rtc.lastClipId);
  strlcpy(rtc.lastClipAgent, agent.c_str(), sizeof rtc.lastClipAgent);
  audio_io::playWav(r.body.p, r.body.len);  // format inconnu : on affiche seulement le texte
  return true;
}

// Traite les evenements dans l'ordre ; renvoie le nombre d'evenements entierement traites.
static size_t processEvents(const std::vector<EventItem> &events) {
  size_t done = 0;
  for (const auto &e : events) {
    if (isMuted(gCfg, e.agent)) { done++; continue; }
    const AgentCfg *a = gCfg.find(e.agent);
    std::string name = a ? a->name : e.agent;
    screenOn();
    ui::alert(name, e.kind, truncateChars(e.text, 80), e.isHigh());
    if (e.kind == "voice" && e.hasClip) {
      if (!downloadAndPlay(e.clip, e.agent)) break;  // non acquitte : rejoue au prochain cycle
      waitOrButton(1500);
    } else {
      audio_io::nudgeTone(e.isHigh());
      waitOrButton(cfg::kAlertShowMs);
    }
    done++;
    if (gPhase != Phase::Run) break;
  }
  audio_io::speakerOff();
  return done;
}

// ---------------- phases ----------------
static bool doRegister() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char hw[40];
  snprintf(hw, sizeof hw, "m5sticks3-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  std::string body = buildRegisterBody(hw, cfg::kModel, buildCapabilitiesJson(cfg::kFirmware));
  net::Resp r(8192);
  net::request(r, HTTP_METHOD_POST, registerUrl(gBase), "", (const uint8_t *)body.data(), body.size(), "application/json");
  if (!r.ok()) {
    ApiError e;
    std::string m = parseApiError(r.text(), e) ? e.message : ("HTTP " + std::to_string(r.code));
    if (gScreenOn) ui::message("Enregistrement", m, TFT_RED);
    return false;
  }
  RegisterResponse rr;
  if (!parseRegister(r.text(), rr)) return false;
  tokenSave(S, rr.token.c_str());
  strlcpy(rtc.pairingCode, rr.pairingCode.c_str(), sizeof rtc.pairingCode);
  rtc.active = 0;
  gPhase = phaseOnRegistered();
  screenOn();  // l'utilisateur doit voir le code
  return true;
}

// Une interrogation de /device/status ; true si l'etat a ete lu.
static bool pollStatus() {
  net::Resp r(2048);
  call(r, HTTP_METHOD_GET, statusUrl(gBase));
  if (handleAuth(r.code)) return true;
  if (!r.ok()) return false;
  gPhase = phaseOnStatus(gPhase, parseStatus(r.text()));
  if (gPhase == Phase::Run) rtc.active = 1;
  return true;
}

static bool doPairing() {
  bool shown = gScreenOn || gButtonWake;
  if (shown) { screenOn(); ui::pairing(rtc.pairingCode, S.serverUrl.c_str()); }
  uint32_t t0 = millis();
  while (true) {
    if (!pollStatus()) return false;
    if (gPhase != Phase::Pairing) return true;
    if (!shown || millis() - t0 > 60000) return true;  // reveil periodique ensuite
    waitOrButton(5000);
    touch();
  }
}

static bool doRun() {
  bool capsSent = false;
  for (int page = 0; page < cfg::kMaxSyncPages; page++) {
    net::Resp r(cfg::kMaxJsonResp);
    call(r, HTTP_METHOD_GET, syncUrl(gBase, rtc.lastAck, rtc.configHash, rtc.capsHash, gBattery));
    if (handleAuth(r.code)) return true;
    if (!r.ok()) {
      if (r.code == 426 && gScreenOn) ui::message("Protocole", "Firmware trop ancien pour le serveur", TFT_RED);
      return false;
    }
    SyncResponse sr;
    if (!parseSync(r.text(), sr)) return false;
    if (sr.capabilitiesRequired && !capsSent) {
      capsSent = true;
      if (!putCapabilities()) return gPhase != Phase::Run;
      page--;  // nouvelle synchronisation immediate, sans consommer de page
      continue;
    }
    if (sr.hasConfig) adoptConfig(sr.config, sr.configHash);
    ScreenDecision d = decideScreen(gCfg, rtcPrevStatuses(), sr.states, sr.events);
    gStates = sr.states;
    rtcStoreStates(sr.states, gCfg);
    gNextSyncS = sr.nextSyncS;
    size_t n = processEvents(sr.events);
    rtc.lastAck = ackAfterProcessing(rtc.lastAck, sr.events, n);
    rtc.failures = 0;
    if (d.screen || gScreenOn) { screenOn(); drawStatus(0); }
    if (n < sr.events.size() || !sr.hasMore || gPhase != Phase::Run) break;
  }
  return true;
}

static bool runPhases() {
  gPhase = phaseAfterBoot(S.token.length() > 0, rtc.active);
  for (int step = 0; step < 5; step++) {
    Phase before = gPhase;
    bool ok = true;
    switch (gPhase) {
      case Phase::Register: ok = doRegister(); break;
      case Phase::Pairing: ok = doPairing(); break;
      case Phase::Run: ok = doRun(); break;
      default: return false;
    }
    if (!ok) return false;
    // On rebondit seulement si la phase a change et qu'il y a une suite utile.
    if (gPhase == before) return true;
  }
  return true;
}

// ---------------- interactions ----------------
static bool heldA() { return M5.BtnA.isPressed(); }
static int gRecMaxMs = cfg::kRecMaxS * 1000;
static std::string gRecAgent;
static void recTick(int ms) {
  static int last = -1000;
  if (ms - last >= 100) { ui::recording(gRecAgent, ms, gRecMaxMs); last = ms; }
}

static void doRecord(const AgentState &s) {
  const AgentCfg *a = gCfg.find(s.agent);
  if (!a || !a->canSendVoice) { ui::message("Vocal", "Envoi non autorise vers cet agent", TFT_RED); waitOrButton(1500); return; }
  gRecAgent = a->name;
  ui::recording(gRecAgent, 0, gRecMaxMs);
  // Le Wi-Fi s'associe pendant l'enregistrement (SPEC 12.3) : lance de facon non bloquante.
  if (!gOnline || WiFi.status() != WL_CONNECTED) { WiFi.mode(WIFI_STA); WiFi.begin(S.wifiSsid.c_str(), S.wifiPass.c_str()); }
  size_t len = 0;
  uint8_t *buf = audio_io::recordWav(heldA, cfg::kRecRate, cfg::kRecMaxS, &len, recTick);
  if (!buf) { ui::message("Vocal", "Message trop court", TFT_ORANGE); waitOrButton(1200); return; }
  ui::message("Envoi...", gRecAgent, TFT_CYAN);
  if (!ensureOnline()) { heap_caps_free(buf); ui::message("Vocal", "Wi-Fi indisponible", TFT_RED); waitOrButton(1500); return; }
  net::Resp r(4096);
  call(r, HTTP_METHOD_POST, voiceUploadUrl(gBase, s.agent), buf, len, "audio/wav");
  heap_caps_free(buf);
  handleAuth(r.code);
  if (r.ok()) ui::message("Envoye", gRecAgent, TFT_GREEN);
  else {
    ApiError e;
    ui::message("Echec envoi", parseApiError(r.text(), e) ? e.message : "HTTP " + std::to_string(r.code), TFT_RED);
  }
  waitOrButton(1500);
}

static void doReplay() {
  if (!rtc.lastClipId[0]) { ui::message("Reecoute", "Aucun message", TFT_ORANGE); waitOrButton(1200); return; }
  if (!ensureOnline()) { ui::message("Reecoute", "Wi-Fi indisponible", TFT_RED); waitOrButton(1500); return; }
  ui::message("Reecoute", rtc.lastClipAgent, TFT_CYAN);
  Clip c; c.id = rtc.lastClipId;
  if (!downloadAndPlay(c, rtc.lastClipAgent)) { ui::message("Reecoute", "Echec du telechargement", TFT_RED); waitOrButton(1500); }
  audio_io::speakerOff();
}

// Menu d'abonnements (SPEC 12.3 / 8.3). A:suivant, A long:muet, B:cocher, B long:enregistrer, inactivite:annule.
static void configMenu() {
  if (!ensureOnline()) { ui::message("Config", "Wi-Fi indisponible", TFT_RED); waitOrButton(1500); return; }
  ui::message("Config", "Chargement...", TFT_CYAN);
  std::string baseHash = rtc.configHash;
  {
    net::Resp r(cfg::kMaxJsonResp);
    call(r, HTTP_METHOD_GET, configUrl(gBase));
    if (handleAuth(r.code)) return;
    ConfigResponse cr;
    if (r.ok() && parseConfigResponse(r.text(), cr)) { adoptConfig(cr.config, cr.configHash); baseHash = rtc.configHash; }
  }
  Config work = gCfg;
  if (work.agents.empty()) { ui::message("Config", "Aucun agent visible", TFT_ORANGE); waitOrButton(1500); return; }
  size_t cur = 0;
  std::string note = work.mode == "auto" ? "Mode auto : muet seul." : "";
  uint32_t last = millis();
  ui::menu(work, cur, note);
  while (millis() - last < (uint32_t)cfg::kMenuIdleMs) {
    M5.update();
    bool dirty = false;
    if (M5.BtnA.wasClicked()) { cur = (cur + 1) % work.agents.size(); dirty = true; }
    if (M5.BtnA.wasHold()) { toggleMuted(work, cur); dirty = true; }
    if (M5.BtnB.wasClicked()) { toggleSubscribed(work, cur); dirty = true; }
    if (M5.BtnB.wasHold()) {
      ui::message("Config", "Enregistrement...", TFT_CYAN);
      net::Resp r(cfg::kMaxJsonResp);
      callJson(r, HTTP_METHOD_PUT, configUrl(gBase), buildConfigPutBody(baseHash, work));
      if (handleAuth(r.code)) return;
      ConfigResponse cr;
      if (r.code == 409) {
        // La config a change cote serveur : on adopte la courante et on laisse l'utilisateur recommencer.
        if (parseConfigResponse(r.text(), cr)) { adoptConfig(cr.config, cr.configHash); baseHash = rtc.configHash; work = gCfg; }
        note = "Conflit : config serveur adoptee, refaites.";
        cur = 0; dirty = true;
      } else if (r.ok()) {
        if (parseConfigResponse(r.text(), cr) && !cr.configHash.empty()) {
          adoptConfig(cr.config, cr.configHash);
        } else {
          // Reponse sans config/hash : on relit la config effective pour obtenir le hash.
          adoptConfig(work, "");
          net::Resp g(cfg::kMaxJsonResp);
          call(g, HTTP_METHOD_GET, configUrl(gBase));
          if (g.ok() && parseConfigResponse(g.text(), cr)) adoptConfig(cr.config, cr.configHash);
        }
        ui::message("Config", "Enregistre", TFT_GREEN);
        waitOrButton(1000);
        return;
      } else {
        ApiError e;
        note = parseApiError(r.text(), e) ? e.message : "Echec HTTP " + std::to_string(r.code);
        dirty = true;
      }
    }
    if (dirty) { last = millis(); ui::menu(work, cur, note); }
    delay(15);
  }
}

static void interactive() {
  size_t sel = 0;
  drawStatus(sel);
  touch();
  while (millis() - gActivity < (uint32_t)S.screenTimeoutS * 1000) {
    M5.update();
    auto v = view();
    bool redraw = false;
    if (M5.BtnA.wasClicked()) { if (!v.empty()) sel = (sel + 1) % v.size(); redraw = true; touch(); }
    if (M5.BtnA.wasHold()) {
      touch();
      if (!v.empty()) doRecord(v[sel < v.size() ? sel : 0]);
      redraw = true; touch();
    }
    if (M5.BtnB.wasClicked()) { touch(); doReplay(); redraw = true; touch(); }
    if (M5.BtnB.wasHold()) { touch(); configMenu(); sel = 0; redraw = true; touch(); }
    if (gPhase == Phase::Register || gPhase == Phase::Pairing) return;
    if (redraw) drawStatus(sel);
    delay(15);
  }
}

// ---------------- sommeil ----------------
static void sleepNow(int seconds) {
  audio_io::speakerOff();
  M5.Mic.end();
  if (gScreenOn) ui::backlightOff();
  net::wifiOff();
  // Evite un reveil immediat si un bouton est encore enfonce.
  uint32_t t0 = millis();
  while ((M5.BtnA.isPressed() || M5.BtnB.isPressed()) && millis() - t0 < 3000) { M5.update(); delay(10); }
  const uint64_t mask = (1ULL << GPIO_NUM_11) | (1ULL << GPIO_NUM_12);  // BtnA / BtnB (actifs a l'etat bas)
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(GPIO_NUM_11); rtc_gpio_pullup_en(GPIO_NUM_12);
  rtc_gpio_pulldown_dis(GPIO_NUM_11); rtc_gpio_pulldown_dis(GPIO_NUM_12);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  Serial.printf("[san] deep sleep %d s (phase %s)\n", seconds, phaseName(gPhase));
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  auto c = M5.config();
  c.serial_baudrate = 115200;
  c.internal_spk = true;
  c.internal_mic = true;
  M5.begin(c);
  ui::begin();
  rtcInit();
  settingsLoad(S);
  gButtonWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
  gBattery = M5.Power.getBatteryLevel();
  if (gBattery < 0 || gBattery > 100) gBattery = -1;

  // Portail : premier demarrage sans config, ou BtnB maintenu 3 s au boot.
  bool wantPortal = !S.configured();
  if (!wantPortal) {
    uint32_t t0 = millis();
    while (M5.BtnB.isPressed() || millis() - t0 < 50) {
      M5.update();
      if (M5.BtnB.isPressed() && millis() - t0 > (uint32_t)cfg::kBootPortalHoldMs) { wantPortal = true; break; }
      if (!M5.BtnB.isPressed() && millis() - t0 > 50) break;
      delay(10);
    }
  }
  if (wantPortal) {
    if (!runConfigPortal(S)) {
      ui::message("Configuration", "Non terminee. Nouvel essai au prochain reveil.", TFT_RED);
      delay(3000);
      gPhase = Phase::Boot;
      sleepNow(600);
    }
    rtcReset();
  }

  if (gButtonWake || wantPortal || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
    screenOn();
    // Affichage immediat depuis le cache RTC, avant tout acces reseau.
    gCfg = rtcLoadConfig();
    gStates = rtcLoadStates();
    if (S.token.length() && rtc.active) drawStatus(0);
    else ui::message("SAN", "Connexion...", TFT_CYAN);
  } else {
    gCfg = rtcLoadConfig();
    gStates = rtcLoadStates();
  }

  bool ok = false;
  if (ensureOnline()) {
    ok = runPhases();
  } else if (gScreenOn) {
    ui::message("Wi-Fi", "Connexion impossible", TFT_RED);
    waitOrButton(2000);
  }
  if (!ok) { if (rtc.failures < 250) rtc.failures++; }

  if (gScreenOn && (gPhase == Phase::Run)) interactive();
  else if (gScreenOn && gPhase == Phase::Pairing) waitOrButton(3000);

  int sleepS = computeSleepSeconds(gPhase == Phase::Run ? gNextSyncS : 60, rtc.failures);
  sleepNow(sleepS);
}

void loop() {}
