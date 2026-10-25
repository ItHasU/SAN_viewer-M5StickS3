// Firmware viewer SAN - M5StickS3. Cycle : reveil -> Wi-Fi -> /viewer/sync -> icones / sons / vocaux -> (fenetre d'eveil) -> deep sleep.
// Toute la logique de decision est dans lib/san_core (testee sur hote) ; ici : orchestration et E/S.
#include <M5Unified.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include <algorithm>

#include "audio_io.h"
#include "config.h"
#include "hw_state.h"
#include "net.h"
#include "portal.h"
#include "san_audio.h"
#include "san_logic.h"
#include "san_board.h"
#include "san_protocol.h"
#include "san_ui.h"
#include "ui.h"

using namespace san;

// ---------------- etat d'execution ----------------
// Tout l'etat mutable du firmware est ici (un reveil = un processus mono-tache : pas de concurrence).
static Settings S;
static struct Runtime {
  Config cfg;
  std::vector<AgentState> states;  // etats bruts (cache RTC ou derniere sync)
  std::vector<AgentState> view;    // etats suivis, ordonnes (cache de orderStates)
  bool viewStale = true;
  std::string base;
  size_t sel = 0;                  // agent selectionne (vocal, enregistrement) ; son slug est memorise en RTC
  bool recording = false;          // enregistrement en cours : aucune auto-selection
  int battery = -1;                // niveau affiche et remonte au serveur (battery_pct)
  Charge charge = Charge::Unknown;
  uint32_t batteryAt = 0;
  bool buttonWake = false, screenOn = false, online = false, serverOk = false, dirty = true;
  uint32_t activity = 0, nextSyncAt = 0;
  int nextSyncS = 0;
  Phase phase = Phase::Boot;
  // Dernier clip vocal garde en PSRAM (reecoute instantanee pendant le meme eveil) ; libere au remplacement.
  uint8_t *clip = nullptr;
  size_t clipLen = 0;
  char clipId[40] = {0};
} G;

static void touch() { G.activity = millis(); }
static void screenOn() {
  if (!G.screenOn) { ui::backlightOn(); G.screenOn = true; }
  touch();
}
static const std::vector<AgentState> &view() {
  if (G.viewStale) {
    G.view = orderStates(G.cfg, G.states);
    G.viewStale = false;
    G.sel = agentIndexBySlug(G.view, rtc.selAgent, G.sel);  // memorisee par slug (RTC) : survit au sommeil et aux reordonnancements
  }
  G.sel = clampAgentIndex(G.sel, G.view.size());
  return G.view;
}
static void invalidateView() { G.viewStale = true; G.dirty = true; }
static void setStates(const std::vector<AgentState> &st) { G.states = st; invalidateView(); }
// Selectionne l'agent `slug` (sans effet s'il n'est pas affiche).
static void selectAgent(const std::string &slug) {
  const auto &v = view();
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].agent == slug) { G.sel = i; strlcpy(rtc.selAgent, slug.c_str(), sizeof rtc.selAgent); G.dirty = true; return; }
}

static void readBattery() {
  int lvl = M5.Power.getBatteryLevel();
  G.battery = (lvl < 0 || lvl > 100) ? -1 : lvl;
  // Etat de charge : fiabilite variable selon le PMIC ; "inconnu" = pas d'eclair.
  auto c = M5.Power.isCharging();
  G.charge = c == m5::Power_Class::is_charging ? Charge::Charging
             : c == m5::Power_Class::is_discharging ? Charge::Discharging : Charge::Unknown;
  G.batteryAt = millis();
}

static ui::MainView buildView() {
  ui::MainView v;
  const auto &vw = view();
  v.row = buildAgentRow(vw, G.sel);
  v.hasSel = !vw.empty();
  if (v.hasSel) v.selName = agentLabel(G.cfg, vw[G.sel], 18);
  v.wifiOk = WiFi.status() == WL_CONNECTED;
  v.serverOk = G.serverOk;
  v.speakerOn = S.volNotif > 0 || S.volVoice > 0;
  v.autoSleep = S.pauseS > 0;
  v.battery = G.battery;
  v.charge = G.charge;
  return v;
}
static void redraw() { ui::mainView(buildView()); G.dirty = false; }

// ---- etat de connexion : visible dans la barre de statut (icones Wi-Fi / serveur), son d'erreur anti-bruit ----
enum class Mode { Main, Menu };
static Mode gMode = Mode::Main;  // ecran courant pendant l'eveil
// L'ecran principal peut etre redessine sans ecraser un autre ecran (menu, code d'appairage).
static bool mainVisible() { return G.screenOn && gMode == Mode::Main && G.phase != Phase::Pairing; }
static void connResult(bool wifi, bool ok) {
  uint8_t &fails = wifi ? rtc.wifiFails : rtc.serverFails;
  if (connReportSound(fails, ok, G.screenOn)) audio_io::requestSound(NudgeType::Error);
  if (mainVisible()) redraw(); else G.dirty = true;
}
static void sysTick() {
  audio_io::housekeeping();  // coupe l'ampli si un son vient de finir pendant une attente bloquante
  if (mainVisible()) ui::mainTick();
}

// ---------------- reseau / API ----------------
static bool ensureOnline() {
  if (G.online && WiFi.status() == WL_CONNECTED) return true;
  G.online = net::wifiConnect(S, cfg::kWifiTimeoutMs, sysTick);
  connResult(true, G.online);
  if (G.online) {
    G.base = chooseBase(S.serverUrl.c_str(), S.lanUrl.c_str(), net::currentSsid(), S.homeSsid.c_str());
    if (G.base.rfind("https://", 0) == 0) net::ensureTime();
    WiFi.setSleep(S.pauseS == 0);  // eveil permanent : economie modem ; sinon latence minimale
  }
  return G.online;
}

static void freeClip() {
  if (G.clip) heap_caps_free(G.clip);
  G.clip = nullptr; G.clipLen = 0; G.clipId[0] = 0;
}

// 401 : efface le jeton et repart de REGISTER (SPEC 3.3 / 12.1). Renvoie true si la phase a change.
static bool handleAuth(int code) {
  Phase np = phaseOnHttp(G.phase, code);
  if (httpCodeClearsToken(code)) {
    tokenClear(S);
    uint8_t ch = rtc.channel; uint8_t bs[6]; memcpy(bs, rtc.bssid, 6);
    rtcReset();  // ack, hashes, config, cache des agents : tout est lie a l'ancien jeton
    rtc.channel = ch; memcpy(rtc.bssid, bs, 6);
    G.cfg = Config();
    freeClip();
    setStates({});
  }
  bool changed = np != G.phase;
  G.phase = np;
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
  G.cfg = c;
  invalidateView();
  rtcStoreConfig(c);
  if (!hash.empty()) strlcpy(rtc.configHash, hash.c_str(), sizeof rtc.configHash);
}

static bool putCapabilities() {
  std::string body = buildCapabilitiesJson(cfg::kFirmware);
  net::Resp r(4096);
  callJson(r, HTTP_METHOD_PUT, capabilitiesUrl(G.base), body);
  if (handleAuth(r.code)) return false;
  if (!r.ok()) return false;
  strlcpy(rtc.capsHash, capsHash(body).c_str(), sizeof rtc.capsHash);
  return true;
}

// ---------------- evenements ----------------
// Attente courte interruptible par un bouton (ecrans de message ; jamais utilisee dans la boucle principale).
static void waitOrButton(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    M5.update();
    audio_io::housekeeping();
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) break;
    delay(20);
  }
}

enum class Fetch { Ok, Gone, Failed };

// Telecharge un clip et le garde en PSRAM (G.clip) pour la lecture et la reecoute.
static Fetch fetchClip(const std::string &id, const std::string &agent) {
  net::Resp r(cfg::kMaxClipBytes);
  call(r, HTTP_METHOD_GET, voiceClipUrl(G.base, id));
  if (handleAuth(r.code)) return Fetch::Failed;
  if (r.code == 404 || r.code == 410) return Fetch::Gone;  // clip purge : inutile de reessayer
  if (!r.ok() || r.body.len == 0) return Fetch::Failed;
  freeClip();
  G.clip = r.body.release(&G.clipLen);
  strlcpy(G.clipId, id.c_str(), sizeof G.clipId);
  strlcpy(rtc.lastClipId, id.c_str(), sizeof rtc.lastClipId);
  strlcpy(rtc.lastClipAgent, agent.c_str(), sizeof rtc.lastClipAgent);
  return Fetch::Ok;
}

// Etat d'un agent dans G.states (pastille / clips en attente), par slug.
static AgentState *stateOf(const std::string &slug) {
  for (auto &s : G.states) if (s.agent == slug) return &s;
  return nullptr;
}

// Vocal lu : le serveur decremente voice_pending (POST /viewer/voice/:clip/played, idempotent), la pastille aussi
// localement. Un echec reseau n'est pas bloquant : la pastille reapparaitra a la synchronisation suivante.
static void markPlayed(const std::string &slug, const std::string &clip) {
  if (G.online) {
    net::Resp r(1024);
    call(r, HTTP_METHOD_POST, voicePlayedUrl(G.base, clip));
    handleAuth(r.code);
  }
  if (AgentState *s = stateOf(slug)) {
    auto it = std::find(s->voiceClips.begin(), s->voiceClips.end(), clip);
    if (it != s->voiceClips.end()) s->voiceClips.erase(it);
    if (s->voicePending > 0) s->voicePending--;
    rtcStoreStates(G.states, G.cfg);
    invalidateView();
  }
}

static std::string gPlayName;
static void playTick(int ms, int total) { ui::playback(gPlayName, ms, total); }

// Ecran de lecture d'un vocal : telecharge si besoin, joue (un appui l'interrompt), puis marque lu si demande.
// Renvoie false si le clip n'a pas pu etre lu.
static bool playVoice(const std::string &slug, const std::string &clip, bool mark) {
  const AgentCfg *a = G.cfg.find(slug);
  gPlayName = a ? a->name : slug;
  screenOn();
  if (!(G.clip && !strcmp(G.clipId, clip.c_str()))) {
    if (!ensureOnline()) { G.dirty = true; return false; }
    ui::message("Vocal", gPlayName, TFT_CYAN);
    Fetch f = fetchClip(clip, slug);
    if (f == Fetch::Gone) {
      markPlayed(slug, clip);  // clip purge : on le retire de la liste
      ui::message("Vocal", "Message indisponible", TFT_RED);
      waitOrButton(1500);
      return false;
    }
    if (f != Fetch::Ok) { ui::message("Vocal", "Echec du telechargement", TFT_RED); waitOrButton(1500); return false; }
  }
  audio_io::soundStop();
  ui::playback(gPlayName, 0, 0);
  audio_io::playWav(G.clip, G.clipLen, true, playTick);  // format inconnu : rien n'est joue
  audio_io::speakerOff();
  if (mark) markPlayed(slug, clip);
  G.dirty = true;
  return true;
}

// Evenements : les nudges declenchent le son marimba de leur type (regles anti-bruit dans audio_io). Le statut et
// l'icone viennent de states[] (le serveur fixe l'etat de l'agent avec le nudge) ; les vocaux, de voice_pending_clips.
// Les evenements "voice" sont donc simplement acquittes. Renvoie le nombre d'evenements traites (a acquitter).
static size_t processEvents(const std::vector<EventItem> &events) {
  for (const auto &e : events)
    if (e.kind != "voice" && S.volNotif > 0) audio_io::requestSound(e.type);
  return events.size();
}

// ---------------- phases ----------------
static bool doRegister() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char hw[40];
  snprintf(hw, sizeof hw, "m5sticks3-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  std::string body = buildRegisterBody(hw, cfg::kModel, buildCapabilitiesJson(cfg::kFirmware));
  net::Resp r(8192);
  net::request(r, HTTP_METHOD_POST, registerUrl(G.base), "", (const uint8_t *)body.data(), body.size(), "application/json");
  if (!r.ok()) {
    ApiError e;
    std::string m = parseApiError(r.text(), e) ? e.message : ("HTTP " + std::to_string(r.code));
    if (G.screenOn && r.code > 0) ui::message("Enregistrement", m, TFT_RED);  // erreur applicative ; transport : notification Serveur
    return false;
  }
  RegisterResponse rr;
  if (!parseRegister(r.text(), rr)) return false;
  tokenSave(S, rr.token.c_str());
  strlcpy(rtc.pairingCode, rr.pairingCode.c_str(), sizeof rtc.pairingCode);
  rtc.active = 0;
  G.phase = phaseOnRegistered();
  screenOn();  // l'utilisateur doit voir le code
  return true;
}

// Une interrogation de /device/status ; true si l'etat a ete lu.
static bool pollStatus() {
  net::Resp r(2048);
  call(r, HTTP_METHOD_GET, statusUrl(G.base));
  if (handleAuth(r.code)) return true;
  if (!r.ok()) return false;
  G.phase = phaseOnStatus(G.phase, parseStatus(r.text()));
  if (G.phase == Phase::Run) rtc.active = 1;
  return true;
}

static bool doPairing() {
  bool shown = G.screenOn || G.buttonWake;
  if (shown) { screenOn(); ui::pairing(rtc.pairingCode); }
  uint32_t t0 = millis();
  while (true) {
    if (!pollStatus()) return false;
    if (G.phase != Phase::Pairing) return true;
    if (!shown || millis() - t0 > 60000) return true;  // reveil periodique ensuite
    waitOrButton(5000);
    touch();
  }
}

static bool doRun() {
  bool capsSent = false;
  for (int page = 0; page < cfg::kMaxSyncPages; page++) {
    net::Resp r(cfg::kMaxJsonResp);
    call(r, HTTP_METHOD_GET, syncUrl(G.base, rtc.lastAck, rtc.configHash, rtc.capsHash, G.battery));
    if (handleAuth(r.code)) return true;
    if (!r.ok()) {
      if (r.code == 426 && G.screenOn) ui::message("Protocole", "Firmware trop ancien pour le serveur", TFT_RED);
      return false;
    }
    SyncResponse sr;
    if (!parseSync(r.text(), sr)) return false;
    if (sr.capabilitiesRequired && !capsSent) {
      capsSent = true;
      if (!putCapabilities()) return G.phase != Phase::Run;
      page--;  // nouvelle synchronisation immediate, sans consommer de page
      continue;
    }
    if (sr.hasConfig) adoptConfig(sr.config, sr.configHash);
    SyncDiff d = diffStates(rtcPrevAgents(), sr.states);  // avant la mise a jour du cache RTC
    setStates(sr.states);
    rtcStoreStates(sr.states, G.cfg);
    rtcUpdateSeen(sr.states);
    G.nextSyncS = sr.nextSyncS;
    G.serverOk = true;
    if (d.changed || !sr.events.empty()) screenOn();
    if (!d.pick.empty() && !G.recording) selectAgent(d.pick);  // vocal > question > probleme > pret ; pas pendant un enregistrement
    if (G.screenOn && G.dirty && mainVisible()) redraw();
    size_t n = processEvents(sr.events);
    rtc.lastAck = ackAfterProcessing(rtc.lastAck, sr.events, n);
    // Lecture automatique des vocaux arrives (volume des vocaux non nul), dans l'ordre d'arrivee.
    if (S.autoPlay && S.volVoice > 0 && !G.recording)
      for (auto &va : d.arrivals)
        for (auto &clip : va.clips) {
          if (G.phase != Phase::Run) break;
          selectAgent(va.agent);
          playVoice(va.agent, clip, true);
        }
    rtc.failures = 0;
    if (!sr.hasMore || G.phase != Phase::Run) break;
  }
  return true;
}

static bool runPhases() {
  G.phase = phaseAfterBoot(S.token.length() > 0, rtc.active);
  for (int step = 0; step < 5; step++) {
    Phase before = G.phase;
    bool ok = true;
    switch (G.phase) {
      case Phase::Register: ok = doRegister(); break;
      case Phase::Pairing: ok = doPairing(); break;
      case Phase::Run: ok = doRun(); break;
      default: return false;
    }
    if (!ok) return false;
    // On rebondit seulement si la phase a change et qu'il y a une suite utile.
    if (G.phase == before) return true;
  }
  return true;
}

// Cycle complet de synchronisation (au reveil, puis periodiquement pendant l'eveil). Planifie la suivante.
static bool syncNow(bool verbose) {
  bool ok = false;
  if (ensureOnline()) {
    ok = runPhases();
    connResult(false, ok);
  }
  G.serverOk = ok;
  if (!ok && rtc.failures < 250) rtc.failures++;
  G.nextSyncAt = millis() + (uint32_t)computeSleepSeconds(G.phase == Phase::Run ? G.nextSyncS : 60, rtc.failures) * 1000u;
  G.dirty = true;
  return ok;
}

// ---------------- interactions ----------------
static bool heldFront() { return SAN_BTN_FRONT.isPressed(); }
static int gRecMaxMs = cfg::kRecMaxS * 1000;
static std::string gRecAgent;
static void recTick(int ms) {
  static int last = -1000;
  if (ms < last) last = -1000;
  if (ms - last >= 100) { ui::recording(gRecAgent, ms, gRecMaxMs); last = ms; }
}

static void doRecord(const AgentState &s) {
  const AgentCfg *a = G.cfg.find(s.agent);
  if (!a || !a->canSendVoice) {
    audio_io::micCancel();
    ui::message("Vocal", "Envoi non autorise vers cet agent", TFT_RED); waitOrButton(1500); return;
  }
  G.recording = true;  // pas d'auto-selection pendant l'enregistrement
  gRecAgent = a->name;
  ui::recording(gRecAgent, 0, gRecMaxMs);
  net::wifiBegin(S);  // le Wi-Fi s'associe pendant l'enregistrement (SPEC 12.3) ; wifiConnect reprend sans relancer
  size_t len = 0;
  // Le micro a ete demarre a l'appui (micPrepare) : transitoire du codec deja ecoule, gain +12 dB si pas de saturation.
  uint8_t *buf = audio_io::recordWav(heldFront, cfg::kRecRate, cfg::kRecMaxS, &len, recTick);
  G.recording = false;
  if (!buf) { ui::message("Vocal", "Message trop court", TFT_ORANGE); waitOrButton(1200); return; }
  if (!ensureOnline()) { heap_caps_free(buf); G.dirty = true; return; }  // la barre de statut dit pourquoi
  ui::message("Envoi...", gRecAgent, TFT_CYAN);
  net::Resp r(4096);
  call(r, HTTP_METHOD_POST, voiceUploadUrl(G.base, s.agent), buf, len, "audio/wav");
  heap_caps_free(buf);
  handleAuth(r.code);
  if (r.code <= 0) { connResult(false, false); G.dirty = true; return; }  // transport : serveur injoignable
  if (r.ok()) { connResult(false, true); ui::message("Envoye", gRecAgent, TFT_GREEN); }
  else {
    ApiError e;
    ui::message("Echec envoi", parseApiError(r.text(), e) ? e.message : "HTTP " + std::to_string(r.code), TFT_RED);
  }
  waitOrButton(1500);
}

// Appui court frontal : lit le plus ancien vocal en attente de l'agent selectionne ; sans vocal en attente, reecoute
// le dernier vocal de cet agent s'il existe.
static void doPlayVoice(const AgentState &s) {
  if (!s.voiceClips.empty()) { playVoice(s.agent, s.voiceClips.front(), true); return; }
  if (s.voicePending > 0) {  // pastille connue (cache RTC) mais liste de clips pas encore recue
    ui::message("Vocal", "Synchronisation en cours", TFT_ORANGE); waitOrButton(1200);
    G.nextSyncAt = millis();
    return;
  }
  if (rtc.lastClipId[0] && s.agent == rtc.lastClipAgent) { playVoice(s.agent, rtc.lastClipId, false); return; }
  ui::message("Vocal", "Aucun vocal", TFT_ORANGE);
  waitOrButton(1200);
}

// ---- menu ----
struct MenuCtx {
  MenuState st;
};

static void drawMenu(const MenuCtx &m) {
  std::vector<ui::MenuRowView> rows;
  for (size_t i = 0; i < kMenuRows; i++) {
    ui::MenuRowView r;
    r.item = (MenuItem)i;
    r.label = menuLabel(r.item);
    switch (r.item) {
      case MenuItem::VolNotif: r.value = formatVolume(S.volNotif); r.muted = S.volNotif <= 0; break;
      case MenuItem::VolVoice: r.value = formatVolume(S.volVoice); r.muted = S.volVoice <= 0; break;
      case MenuItem::Brightness: r.value = formatBrightness(S.brightness); break;
      case MenuItem::Pause: r.value = formatPause(S.pauseS); break;
      case MenuItem::AutoPlay: r.value = formatYesNo(S.autoPlay); break;
      default: break;
    }
    rows.push_back(r);
  }
  ui::menu(m.st.cursor, rows);
}

// Evenement de menu d'apres les boutons (aucun si rien). Dessus = element suivant / quitter, frontal = agir.
static bool menuEventFromButtons(MenuEvent &e) {
  if (SAN_BTN_TOP.wasClicked()) { e = MenuEvent::TopClick; return true; }
  if (SAN_BTN_TOP.wasHold()) { e = MenuEvent::TopHold; return true; }
  if (SAN_BTN_FRONT.wasClicked()) { e = MenuEvent::FrontClick; return true; }
  if (SAN_BTN_FRONT.wasHold()) { e = MenuEvent::FrontHold; return true; }
  return false;
}

// Config Wi-Fi depuis le menu : coupe la connexion, ouvre le portail (AP) sans redemarrer. Au retour (enregistre,
// annule ou timeout) la carte reprend son fonctionnement normal : nouvelle synchronisation immediate.
static void doWifiConfig() {
  Serial.println("[san] config Wi-Fi (AP) demandee depuis le menu");
  audio_io::soundStop();
  net::wifiOff();
  G.online = false;
  PortalResult r = runConfigPortal(S, true);
  Serial.printf("[san] config Wi-Fi : %s\n", r == PortalResult::Saved ? "enregistree" : r == PortalResult::Cancelled ? "annulee" : "timeout");
  rtc.channel = 0;  // cache canal / BSSID invalide (le reseau a pu changer)
  G.nextSyncAt = millis();
  touch();
  G.dirty = true;
}

// Confirmation en deux etapes : (1) choix de la ligne (deja fait), (2) bouton frontal maintenu kResetHoldMs
// avec barre et compte a rebours. Relacher remet a zero ; bouton du dessus ou 15 s d'inactivite = annulation.
static bool confirmReset() {
  ui::resetConfirm(0, 3, false);
  uint32_t idle0 = millis(), holdStart = 0;
  bool holding = false;
  int lastPct = -1;
  while (millis() - idle0 < 15000) {
    M5.update();
    audio_io::housekeeping();
    if (SAN_BTN_TOP.wasPressed()) return false;
    bool pr = SAN_BTN_FRONT.isPressed();
    if (pr && !holding) { holding = true; holdStart = millis(); lastPct = -1; }
    if (!pr && holding) { holding = false; idle0 = millis(); ui::resetConfirm(0, 3, false); }
    if (holding) {
      uint32_t held = millis() - holdStart;
      if (resetConfirmed(held)) return true;
      int pct = resetProgress(held);
      if (pct != lastPct) { lastPct = pct; ui::resetConfirm(pct, resetSecondsLeft(held), true); }
    }
    delay(15);
  }
  return false;
}

// Reinitialisation d'usine : NVS (Wi-Fi, URL, jeton, reglages) + RTC (file, hashes, cache), puis redemarrage.
// Le serveur n'est pas modifie : l'ancien enregistrement reste a revoquer par l'administrateur.
[[noreturn]] static void doFactoryReset() {
  Serial.println("[san] REINITIALISATION : effacement NVS + RTC, redemarrage en mode portail");
  audio_io::soundStop();
  settingsEraseAll(S);
  rtcReset();
  ui::message("Reinitialise", "Redemarrage...", TFT_RED);
  delay(1200);
  net::wifiOff();
  Serial.flush();
  ESP.restart();
  while (true) delay(1000);
}

// Applique l'action du menu. Renvoie true si le menu doit se fermer.
static bool menuApply(MenuCtx &m, MenuAct act) {
  switch (act) {
    case MenuAct::VolNotifNext:
      S.volNotif = nextVolume(S.volNotif);
      audio_io::setVolumes(S.volNotif, S.volVoice);
      settingsSaveUi(S);
      audio_io::soundPreview(NudgeType::Notification);  // apercu : son marimba
      break;
    case MenuAct::VolVoiceNext:
      S.volVoice = nextVolume(S.volVoice);
      audio_io::setVolumes(S.volNotif, S.volVoice);
      settingsSaveUi(S);
      audio_io::beep(S.volVoice);  // apercu : bip
      break;
    case MenuAct::BrightnessNext:
      S.brightness = nextBrightness(S.brightness);
      ui::setBrightness(S.brightness);
      settingsSaveUi(S);
      break;
    case MenuAct::PauseNext:
      S.pauseS = nextPause(S.pauseS);
      settingsSaveUi(S);
      break;
    case MenuAct::AutoPlayToggle:
      S.autoPlay = !S.autoPlay;
      settingsSaveUi(S);
      break;
    case MenuAct::WifiConfig: doWifiConfig(); return true;
    case MenuAct::AskReset:
      if (confirmReset()) doFactoryReset();
      break;
    case MenuAct::Close: return true;
    default: break;
  }
  return false;
}

// ---- fenetre d'eveil : vue principale, menu, synchronisations periodiques ----
// Sort quand l'inactivite depasse le delai de pause (jamais si pauseS == 0) ou si la phase n'est plus RUN.
static void awakeLoop() {
  Mode &mode = gMode;
  mode = Mode::Main;
  MenuCtx menu;
  uint32_t menuLast = 0;
  touch();
  G.dirty = true;
  while (true) {
    M5.update();
    audio_io::housekeeping();
    if (G.phase != Phase::Run) return;
    uint32_t now = millis();

    if (mode == Mode::Main) {
      if (S.pauseS > 0 && now - G.activity >= (uint32_t)S.pauseS * 1000u) return;
      // Boutons : dessus = agent suivant (clic) / parametres (long) ; frontal = vocal (clic) / enregistrement (long).
      if (SAN_BTN_FRONT.wasPressed()) {  // micro demarre des l'appui : la montee du codec passe pendant les 500 ms d'attente
        const auto &v = view();
        const AgentCfg *a = v.empty() ? nullptr : G.cfg.find(v[G.sel].agent);
        if (a && a->canSendVoice) audio_io::micPrepare();
      }
      Press p; Button b; bool ev = false;
      if (SAN_BTN_TOP.wasClicked()) { b = Button::Top; p = Press::Click; ev = true; }
      else if (SAN_BTN_TOP.wasHold()) { b = Button::Top; p = Press::Hold; ev = true; }
      else if (SAN_BTN_FRONT.wasClicked()) { b = Button::Front; p = Press::Click; ev = true; audio_io::micCancel(); }
      else if (SAN_BTN_FRONT.wasHold()) { b = Button::Front; p = Press::Hold; ev = true; }
      if (ev) {
        touch();
        audio_io::soundStop();  // un appui coupe le son d'alerte en cours
        switch (mainAction(b, p)) {
          case MainAction::NextAgent: {
            const auto &v = view();
            G.sel = nextAgentIndex(G.sel, v.size());
            if (!v.empty()) strlcpy(rtc.selAgent, v[G.sel].agent.c_str(), sizeof rtc.selAgent);
            break;
          }
          case MainAction::PlayVoice: {
            const AgentState *s = view().empty() ? nullptr : &view()[G.sel];
            if (s) { AgentState copy = *s; doPlayVoice(copy); }
            break;
          }
          case MainAction::Record: {
            const AgentState *s = view().empty() ? nullptr : &view()[G.sel];
            if (s) { AgentState copy = *s; doRecord(copy); }
            else audio_io::micCancel();
            break;
          }
          case MainAction::OpenMenu:
            menu = MenuCtx();
            mode = Mode::Menu;
            ui::stopAnim();
            menuLast = millis();
            drawMenu(menu);
            break;
          default: break;
        }
        if (mode == Mode::Main) { touch(); G.dirty = true; }
        if (G.phase != Phase::Run) return;
      }
      // Synchronisation periodique (next_sync_s du serveur) pendant l'eveil.
      if ((int32_t)(now - G.nextSyncAt) >= 0) {
        syncNow(false);
        if (G.phase != Phase::Run) return;
      }
      if (now - G.batteryAt > (uint32_t)cfg::kBatteryRefreshMs) {
        int before = G.battery; Charge cb = G.charge;
        readBattery();
        if (before != G.battery || cb != G.charge) G.dirty = true;
      }
      if (G.dirty) redraw();
      else ui::mainTick();
    } else {
      MenuEvent e;
      MenuAct act = MenuAct::None;
      if (menuEventFromButtons(e)) { menuLast = millis(); act = menuStep(menu.st, e); }
      else if (millis() - menuLast > (uint32_t)cfg::kMenuIdleMs) act = menuStep(menu.st, MenuEvent::Idle);
      if (act != MenuAct::None) {
        if (menuApply(menu, act)) {
          mode = Mode::Main;
          touch();
          G.dirty = true;
          continue;
        }
        menuLast = millis();
        drawMenu(menu);
      }
    }
    delay(15);
  }
}

// ---------------- sommeil ----------------
static void sleepNow(int seconds) {
  for (uint32_t t0 = millis(); (audio_io::soundBusy() || audio_io::soundPending()) && millis() - t0 < 6000;) {
    audio_io::housekeeping();  // laisse finir (et lancer) les sons en attente avant de dormir
    delay(5);
  }
  audio_io::soundWait();
  audio_io::quiesce();
  M5.Mic.end();
  freeClip();
  if (G.screenOn) ui::backlightOff();
  net::wifiOff();
  // Evite un reveil immediat si un bouton est encore enfonce.
  uint32_t t0 = millis();
  while ((M5.BtnA.isPressed() || M5.BtnB.isPressed()) && millis() - t0 < 3000) { M5.update(); delay(10); }
  const gpio_num_t pins[] = {GPIO_NUM_11, GPIO_NUM_12};  // BtnA / BtnB (actifs a l'etat bas)
  uint64_t mask = 0;
  for (auto p : pins) {
    mask |= 1ULL << p;
    rtc_gpio_init(p);
    rtc_gpio_set_direction(p, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(p);
    rtc_gpio_pulldown_dis(p);
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  Serial.printf("[san] deep sleep %d s (phase %s)\n", seconds, phaseName(G.phase));
  Serial.flush();
  esp_deep_sleep_start();
}

// Bouton du dessus maintenu kBootPortalHoldMs des le demarrage : portail de configuration.
static bool portalRequested() {
  uint32_t t0 = millis();
  while (millis() - t0 < 60) { M5.update(); delay(10); }  // laisse le temps d'etablir l'etat du bouton
  while (SAN_BTN_TOP.isPressed()) {
    if (millis() - t0 > (uint32_t)cfg::kBootPortalHoldMs) return true;
    M5.update();
    delay(10);
  }
  return false;
}

void setup() {
  auto c = M5.config();
  c.serial_baudrate = 115200;
  c.internal_spk = true;
  c.internal_mic = true;
  M5.begin(c);
  SAN_BTN_TOP.setHoldThresh(cfg::kHoldMs);
  SAN_BTN_FRONT.setHoldThresh(cfg::kHoldMs);
  ui::begin();
  rtcInit();
  settingsLoad(S);
  audio_io::quiesce();  // ampli + codec coupes tant qu'aucun son n'est joue
  audio_io::setVolumes(S.volNotif, S.volVoice);
  ui::setBrightness(S.brightness);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  G.buttonWake = cause == ESP_SLEEP_WAKEUP_EXT1;
  readBattery();

  // Portail : premier demarrage sans config, ou bouton du dessus maintenu 3 s au demarrage (hors reveil timer).
  bool wantPortal = !S.configured() || (cause != ESP_SLEEP_WAKEUP_TIMER && portalRequested());
  if (wantPortal) {
    bool first = !S.configured();
    PortalResult pr = runConfigPortal(S, !first);  // annulable seulement si une config existe deja
    if (pr == PortalResult::Saved) {
      rtcReset();
    } else if (first) {
      ui::message("Configuration", "Non terminee. Nouvel essai au prochain reveil.", TFT_RED);
      G.screenOn = true;
      delay(3000);
      G.phase = Phase::Boot;
      sleepNow(600);
    }  // sinon : annule / timeout avec une config valide : fonctionnement normal
  }

  G.cfg = rtcLoadConfig();
  G.states = rtcLoadStates();
  G.viewStale = true;
  G.serverOk = rtc.failures == 0 && rtc.active;  // optimiste jusqu'a la premiere synchronisation
  const bool neverSleep = S.pauseS == 0;
  G.phase = phaseAfterBoot(S.token.length() > 0, rtc.active);

  if (G.buttonWake || wantPortal || cause == ESP_SLEEP_WAKEUP_UNDEFINED || neverSleep) {
    screenOn();
    // Affichage immediat depuis le cache RTC, avant tout acces reseau.
    redraw();  // vue principale (cache RTC ou vide) ; l'etat reseau est dans la barre de statut
  }

  syncNow(true);

  if (G.phase == Phase::Run && (G.screenOn || neverSleep)) {
    screenOn();
    awakeLoop();
  } else if (G.screenOn) {
    if (G.phase != Phase::Pairing) redraw();  // laisse voir l'erreur de connexion avant de dormir
    waitOrButton(3000);
  }

  sleepNow(computeSleepSeconds(G.phase == Phase::Run ? G.nextSyncS : 60, rtc.failures));
}

void loop() {}
