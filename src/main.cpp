// Firmware viewer SAN - M5StickS3. Cycle : reveil -> Wi-Fi -> /viewer/sync -> notifications -> (fenetre d'eveil) -> deep sleep.
// Toute la logique de decision est dans lib/san_core (testee sur hote) ; ici : orchestration et E/S.
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
#include "san_notify.h"
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
  NotifQueue notifs;               // notifications en attente (persistees en RTC)
  std::string base;
  size_t sel = 0;                  // agent selectionne (cible de l'enregistrement)
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
  if (G.viewStale) { G.view = orderStates(G.cfg, G.states); G.viewStale = false; }
  G.sel = clampAgentIndex(G.sel, G.view.size());
  return G.view;
}
static void invalidateView() { G.viewStale = true; G.dirty = true; }
static void setStates(const std::vector<AgentState> &st) { G.states = st; invalidateView(); }
static void persistNotifs() { rtcStoreNotifs(G.notifs); }

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
  for (auto &n : G.notifs.latest(cfg::kNotifLines)) {
    const AgentCfg *a = G.cfg.find(n.agent);
    ui::NotifLine l;
    l.name = a ? a->name : n.agent;
    l.type = n.type; l.voice = n.voice; l.high = n.high; l.count = n.count;
    v.notifs.push_back(l);
  }
  v.pending = G.notifs.total();
  const auto &vw = view();
  v.activity = summarizeActivity(vw);
  if (!vw.empty()) {
    const AgentState &s = vw[G.sel];
    v.hasSelected = true;
    v.selName = s.name.empty() ? s.agent : s.name;
    v.selStatus = s.status;
    v.selStale = s.stale;
    v.selProgress = s.progress;
  }
  v.wifiOk = WiFi.status() == WL_CONNECTED;
  v.serverOk = G.serverOk;
  v.speakerOn = S.volume > 0;
  v.autoSleep = S.pauseS > 0;
  v.battery = G.battery;
  v.charge = G.charge;
  return v;
}
static void redraw() { ui::mainView(buildView()); G.dirty = false; }

// ---------------- reseau / API ----------------
static bool ensureOnline() {
  if (G.online && WiFi.status() == WL_CONNECTED) return true;
  G.online = net::wifiConnect(S, cfg::kWifiTimeoutMs);
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
    rtcReset();  // ack, hashes, config, notifications : tout est lie a l'ancien jeton
    rtc.channel = ch; memcpy(rtc.bssid, bs, 6);
    G.cfg = Config();
    G.notifs = NotifQueue();
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

static void playClip() {
  if (!G.clip) return;
  audio_io::playWav(G.clip, G.clipLen);  // format inconnu : rien n'est joue, la notification reste affichee
  audio_io::speakerOff();
}

// Traite les evenements dans l'ordre ; renvoie le nombre d'evenements entierement traites (a acquitter).
// Un nudge est ajoute a la file locale (puis acquitte cote serveur) ; un vocal est telecharge, ajoute, puis joue.
static size_t processEvents(const std::vector<EventItem> &events) {
  size_t done = 0;
  std::vector<EventItem> alerted;
  for (const auto &e : events) {
    if (isMuted(G.cfg, e.agent)) { done++; continue; }
    bool voice = e.kind == "voice" && e.hasClip;
    if (voice) {
      Fetch f = fetchClip(e.clip.id, e.agent);
      if (f == Fetch::Failed) break;  // non acquitte : rejoue au prochain cycle
      if (f == Fetch::Gone) { done++; continue; }
    }
    G.notifs.push(e.agent, e.type, voice, e.isHigh(), e.id);
    persistNotifs();
    screenOn();
    redraw();
    if (voice) playClip();
    else alerted.push_back(e);
    done++;
    if (G.phase != Phase::Run) break;
  }
  NudgeType t; bool high;
  if (pickAlert(alerted, t, high)) audio_io::soundStart(t, high);  // non bloquant : la boucle l'entretient
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
  net::request(r, HTTP_METHOD_POST, registerUrl(G.base), "", (const uint8_t *)body.data(), body.size(), "application/json");
  if (!r.ok()) {
    ApiError e;
    std::string m = parseApiError(r.text(), e) ? e.message : ("HTTP " + std::to_string(r.code));
    if (G.screenOn) ui::message("Enregistrement", m, TFT_RED);
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
  if (shown) { screenOn(); ui::pairing(rtc.pairingCode, S.serverUrl.c_str()); }
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
    ScreenDecision d = decideScreen(G.cfg, rtcPrevStatuses(), sr.states, sr.events);
    setStates(sr.states);
    rtcStoreStates(sr.states, G.cfg);
    G.nextSyncS = sr.nextSyncS;
    G.serverOk = true;
    if (d.screen) screenOn();
    size_t n = processEvents(sr.events);
    rtc.lastAck = ackAfterProcessing(rtc.lastAck, sr.events, n);
    rtc.failures = 0;
    if (n < sr.events.size() || !sr.hasMore || G.phase != Phase::Run) break;
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
  } else if (verbose && G.screenOn) {
    ui::message("Wi-Fi", "Connexion impossible", TFT_RED);
    waitOrButton(2000);
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
  if (!a || !a->canSendVoice) { ui::message("Vocal", "Envoi non autorise vers cet agent", TFT_RED); waitOrButton(1500); return; }
  gRecAgent = a->name;
  ui::recording(gRecAgent, 0, gRecMaxMs);
  net::wifiBegin(S);  // le Wi-Fi s'associe pendant l'enregistrement (SPEC 12.3) ; wifiConnect reprend sans relancer
  size_t len = 0;
  uint8_t *buf = audio_io::recordWav(heldFront, cfg::kRecRate, cfg::kRecMaxS, &len, recTick);
  if (!buf) { ui::message("Vocal", "Message trop court", TFT_ORANGE); waitOrButton(1200); return; }
  ui::message("Envoi...", gRecAgent, TFT_CYAN);
  if (!ensureOnline()) { heap_caps_free(buf); ui::message("Vocal", "Wi-Fi indisponible", TFT_RED); waitOrButton(1500); return; }
  net::Resp r(4096);
  call(r, HTTP_METHOD_POST, voiceUploadUrl(G.base, s.agent), buf, len, "audio/wav");
  heap_caps_free(buf);
  handleAuth(r.code);
  if (r.ok()) ui::message("Envoye", gRecAgent, TFT_GREEN);
  else {
    ApiError e;
    ui::message("Echec envoi", parseApiError(r.text(), e) ? e.message : "HTTP " + std::to_string(r.code), TFT_RED);
  }
  waitOrButton(1500);
}

// Repete le dernier vocal : instantane s'il est encore en PSRAM, sinon retelechargement.
static void doReplay() {
  if (!G.clip) {
    if (!rtc.lastClipId[0]) { ui::message("Reecoute", "Aucun message", TFT_ORANGE); waitOrButton(1200); return; }
    if (!ensureOnline()) { ui::message("Reecoute", "Wi-Fi indisponible", TFT_RED); waitOrButton(1500); return; }
    ui::message("Reecoute", rtc.lastClipAgent, TFT_CYAN);
    if (fetchClip(rtc.lastClipId, rtc.lastClipAgent) != Fetch::Ok) {
      ui::message("Reecoute", "Message indisponible", TFT_RED);
      waitOrButton(1500);
      return;
    }
  }
  playClip();
}

// ---- menu ----
enum class Mode { Main, MenuRoot, MenuAgents };

struct MenuCtx {
  MenuState st;
  Config work;           // copie editable des abonnements
  std::string baseHash;  // hash de la config sur laquelle on s'est base (PUT)
  std::string note;
};

static void drawMenu(const MenuCtx &m) {
  if (m.st.screen == MenuScreen::Root)
    ui::menuRoot(m.st.cursor, formatVolume(S.volume), formatBrightness(S.brightness), formatPause(S.pauseS),
                 std::to_string(G.cfg.agents.size()));
  else
    ui::menuAgents(m.work, m.st.cursor, m.note);
}

// Charge la config courante du serveur pour l'edition. false : echec (note renseignee).
static bool menuLoadConfig(MenuCtx &m) {
  if (!ensureOnline()) { m.note = "Wi-Fi indisponible"; return false; }
  ui::message("Agents", "Chargement...", TFT_CYAN);
  net::Resp r(cfg::kMaxJsonResp);
  call(r, HTTP_METHOD_GET, configUrl(G.base));
  if (handleAuth(r.code)) return false;
  ConfigResponse cr;
  if (r.ok() && parseConfigResponse(r.text(), cr)) adoptConfig(cr.config, cr.configHash);
  else if (!r.ok()) { m.note = "Echec HTTP " + std::to_string(r.code); return false; }
  m.baseHash = rtc.configHash;
  m.work = G.cfg;
  if (m.work.agents.empty()) { m.note = "Aucun agent visible"; return false; }
  m.note = m.work.mode == "auto" ? "Mode auto : muet seul" : "";
  return true;
}

// Enregistre les abonnements (SPEC 8.3). true : enregistre. 409 : config serveur adoptee, a refaire.
static bool menuSaveConfig(MenuCtx &m) {
  ui::message("Agents", "Enregistrement...", TFT_CYAN);
  net::Resp r(cfg::kMaxJsonResp);
  callJson(r, HTTP_METHOD_PUT, configUrl(G.base), buildConfigPutBody(m.baseHash, m.work));
  if (handleAuth(r.code)) return false;
  ConfigResponse cr;
  if (r.code == 409) {
    if (parseConfigResponse(r.text(), cr)) { adoptConfig(cr.config, cr.configHash); m.baseHash = rtc.configHash; m.work = G.cfg; }
    m.note = "Conflit : refaites";
    m.st.cursor = 0;
    return false;
  }
  if (!r.ok()) {
    ApiError e;
    m.note = parseApiError(r.text(), e) ? e.message : "Echec HTTP " + std::to_string(r.code);
    return false;
  }
  if (parseConfigResponse(r.text(), cr) && !cr.configHash.empty()) {
    adoptConfig(cr.config, cr.configHash);
  } else {
    // Reponse sans config/hash : on relit la config effective pour obtenir le hash.
    adoptConfig(m.work, "");
    net::Resp g(cfg::kMaxJsonResp);
    call(g, HTTP_METHOD_GET, configUrl(G.base));
    if (g.ok() && parseConfigResponse(g.text(), cr)) adoptConfig(cr.config, cr.configHash);
  }
  G.sel = 0;
  return true;
}

// Evenement de menu d'apres les boutons (aucun si rien). Frontal = defiler / retour, dessus = agir.
static bool menuEventFromButtons(MenuEvent &e) {
  if (SAN_BTN_FRONT.wasSingleClicked()) { e = MenuEvent::FrontClick; return true; }
  if (SAN_BTN_FRONT.wasHold()) { e = MenuEvent::FrontHold; return true; }
  if (SAN_BTN_TOP.wasClicked()) { e = MenuEvent::TopClick; return true; }
  if (SAN_BTN_TOP.wasHold()) { e = MenuEvent::TopHold; return true; }
  return false;
}

// Applique l'action du menu. Renvoie true si le menu doit se fermer.
static bool menuApply(MenuCtx &m, MenuAct act) {
  switch (act) {
    case MenuAct::VolumeNext:
      S.volume = nextVolume(S.volume);
      audio_io::setVolume(S.volume);
      settingsSaveUi(S);
      audio_io::beep();  // apercu sonore
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
    case MenuAct::OpenAgents:
      if (menuLoadConfig(m)) menuEnterAgents(m.st, m.work.agents.size());
      else { ui::message("Agents", m.note, TFT_ORANGE); waitOrButton(1500); m.note.clear(); }
      break;
    case MenuAct::ToggleAgent: toggleSubscribed(m.work, m.st.cursor); break;
    case MenuAct::ToggleMute: toggleMuted(m.work, m.st.cursor); break;
    case MenuAct::SaveAgents:
      if (menuSaveConfig(m)) {
        ui::message("Agents", "Enregistre", TFT_GREEN);
        waitOrButton(1000);
        menuBackToRoot(m.st);
      }
      break;
    case MenuAct::Close: return true;
    default: break;
  }
  return false;
}

// ---- fenetre d'eveil : vue principale, menu, synchronisations periodiques ----
// Sort quand l'inactivite depasse le delai de pause (jamais si pauseS == 0) ou si la phase n'est plus RUN.
static void awakeLoop() {
  Mode mode = Mode::Main;
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
      Press p; Button b; bool ev = false;
      if (SAN_BTN_TOP.wasClicked()) { b = Button::Top; p = Press::Click; ev = true; }
      else if (SAN_BTN_TOP.wasHold()) { b = Button::Top; p = Press::Hold; ev = true; }
      else if (SAN_BTN_FRONT.wasSingleClicked()) { b = Button::Front; p = Press::Click; ev = true; }
      else if (SAN_BTN_FRONT.wasDoubleClicked()) { b = Button::Front; p = Press::DoubleClick; ev = true; }
      else if (SAN_BTN_FRONT.wasHold()) { b = Button::Front; p = Press::Hold; ev = true; }
      if (ev) {
        touch();
        audio_io::soundStop();  // un appui coupe le son d'alerte en cours
        switch (mainAction(b, p, !G.notifs.empty())) {
          case MainAction::AckLatest: G.notifs.ackLatest(); persistNotifs(); break;
          case MainAction::NextAgent: G.sel = nextAgentIndex(G.sel, view().size()); break;
          case MainAction::Replay: doReplay(); break;
          case MainAction::Record: {
            const auto &v = view();
            if (!v.empty()) doRecord(v[G.sel]);
            break;
          }
          case MainAction::OpenMenu:
            menu = MenuCtx();
            mode = Mode::MenuRoot;
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
        mode = menu.st.screen == MenuScreen::Root ? Mode::MenuRoot : Mode::MenuAgents;
        drawMenu(menu);
        if (act != MenuAct::ToggleAgent && act != MenuAct::ToggleMute) menu.note.clear();
      }
    }
    delay(15);
  }
}

// ---------------- sommeil ----------------
static void sleepNow(int seconds) {
  audio_io::soundWait();  // laisse finir un son en cours avant de dormir
  audio_io::speakerOff();
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
  Serial.printf("[san] deep sleep %d s (phase %s, %d notif.)\n", seconds, phaseName(G.phase), G.notifs.total());
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
  audio_io::setVolume(S.volume);
  ui::setBrightness(S.brightness);
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  G.buttonWake = cause == ESP_SLEEP_WAKEUP_EXT1;
  readBattery();

  // Portail : premier demarrage sans config, ou bouton du dessus maintenu 3 s au demarrage (hors reveil timer).
  bool wantPortal = !S.configured() || (cause != ESP_SLEEP_WAKEUP_TIMER && portalRequested());
  if (wantPortal) {
    if (!runConfigPortal(S)) {
      ui::message("Configuration", "Non terminee. Nouvel essai au prochain reveil.", TFT_RED);
      G.screenOn = true;
      delay(3000);
      G.phase = Phase::Boot;
      sleepNow(600);
    }
    rtcReset();
  }

  G.cfg = rtcLoadConfig();
  G.states = rtcLoadStates();
  G.notifs = rtcLoadNotifs();
  G.viewStale = true;
  G.serverOk = rtc.failures == 0 && rtc.active;  // optimiste jusqu'a la premiere synchronisation
  const bool neverSleep = S.pauseS == 0;
  G.phase = phaseAfterBoot(S.token.length() > 0, rtc.active);

  if (G.buttonWake || wantPortal || cause == ESP_SLEEP_WAKEUP_UNDEFINED || neverSleep) {
    screenOn();
    // Affichage immediat depuis le cache RTC, avant tout acces reseau.
    if (S.token.length() && rtc.active) redraw();
    else ui::message("SAN", "Connexion...", TFT_CYAN);
  }

  syncNow(true);

  if (G.phase == Phase::Run && (G.screenOn || neverSleep)) {
    screenOn();
    awakeLoop();
  } else if (G.screenOn && G.phase == Phase::Pairing) {
    waitOrButton(3000);
  }

  sleepNow(computeSleepSeconds(G.phase == Phase::Run ? G.nextSyncS : 60, rtc.failures));
}

void loop() {}
