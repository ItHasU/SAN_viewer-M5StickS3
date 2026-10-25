#include "hw_state.h"

#include <Preferences.h>

#include <cstring>

static const char *NS = "san";

void settingsLoad(Settings &s) {
  Preferences p;
  p.begin(NS, true);
  s.token = p.getString("token", "");
  s.serverUrl = p.getString("srv", "");
  s.lanUrl = p.getString("lan", "");
  s.wifiSsid = p.getString("ssid", "");
  s.wifiPass = p.getString("pass", "");
  s.homeSsid = p.getString("home", "");
  int oldVol = p.getInt("vol", san::kDefaultVolume);  // ancien volume unique : repris pour les deux
  s.volNotif = san::sanitizeVolume(p.getInt("voln", oldVol));
  s.volVoice = san::sanitizeVolume(p.getInt("volv", oldVol));
  s.autoPlay = p.getBool("auto", san::kDefaultAutoPlay);
  s.brightness = san::sanitizeBrightness(p.getInt("bri", san::kDefaultBrightness));
  s.pauseS = san::sanitizePause(p.getInt("pause", san::kDefaultPauseS));
  p.end();
}

void settingsSave(const Settings &s) {
  Preferences p;
  p.begin(NS, false);
  p.putString("srv", s.serverUrl);
  p.putString("lan", s.lanUrl);
  p.putString("ssid", s.wifiSsid);
  p.putString("pass", s.wifiPass);
  p.putString("home", s.homeSsid);
  p.end();
}

void settingsSaveUi(const Settings &s) {
  Preferences p;
  p.begin(NS, false);
  p.putInt("voln", s.volNotif);
  p.putInt("volv", s.volVoice);
  p.putBool("auto", s.autoPlay);
  p.putInt("bri", s.brightness);
  p.putInt("pause", s.pauseS);
  p.end();
}

void settingsEraseAll(Settings &s) {
  Preferences p;
  p.begin(NS, false);
  p.clear();  // Wi-Fi, URLs, jeton, volume / luminosite / pause
  p.end();
  s = Settings();
}

void tokenSave(Settings &s, const String &token) {
  s.token = token;
  Preferences p;
  p.begin(NS, false);
  p.putString("token", token);
  p.end();
}
void tokenClear(Settings &s) { tokenSave(s, ""); }

// ---- RTC ----
static const uint32_t kMagic = 0x53414E06;
RTC_DATA_ATTR RtcState rtc;

void rtcReset() {
  memset(&rtc, 0, sizeof rtc);
  rtc.magic = kMagic;
}
void rtcInit() {
  if (rtc.magic != kMagic) rtcReset();
}

static void cpy(char *dst, size_t n, const std::string &s) {
  strncpy(dst, s.c_str(), n - 1);
  dst[n - 1] = 0;
}

static uint8_t statusCode(const std::string &s) {
  if (s == "idle") return 0;
  if (s == "working") return 1;
  if (s == "waiting") return 2;
  if (s == "error") return 3;
  if (s == "done") return 4;
  return 255;
}
static const char *statusName(uint8_t c) {
  static const char *n[] = {"idle", "working", "waiting", "error", "done"};
  return c < 5 ? n[c] : "";
}

void rtcStoreConfig(const san::Config &c) {
  cpy(rtc.mode, sizeof rtc.mode, c.mode);
  // Conserve les etats existants par slug quand c'est possible.
  RtcAgent old[cfg::kMaxAgents];
  memcpy(old, rtc.agents, sizeof old);
  int oldN = rtc.nAgents;
  memset(rtc.agents, 0, sizeof rtc.agents);
  int n = 0;
  for (auto &a : c.agents) {
    if (n >= cfg::kMaxAgents) break;
    RtcAgent &r = rtc.agents[n];
    cpy(r.slug, sizeof r.slug, a.agent);
    cpy(r.name, sizeof r.name, a.name);
    r.flags = a.canSendVoice ? 8 : 0;
    r.status = 255;
    for (int i = 0; i < oldN; i++)
      if (!strcmp(old[i].slug, r.slug)) {
        r.status = old[i].status; r.stale = old[i].stale; r.progress = old[i].progress;
        r.voices = old[i].voices; r.nSeen = old[i].nSeen; memcpy(r.seen, old[i].seen, sizeof r.seen);
        break;
      }
    n++;
  }
  rtc.nAgents = n;
}

san::Config rtcLoadConfig() {
  san::Config c;
  c.mode = rtc.mode;
  for (int i = 0; i < rtc.nAgents; i++) {
    san::AgentCfg a;
    a.agent = rtc.agents[i].slug;
    a.name = rtc.agents[i].name;
    a.position = i;
    a.canSendVoice = rtc.agents[i].flags & 8;
    c.agents.push_back(a);
  }
  return c;
}

void rtcStoreStates(const std::vector<san::AgentState> &states, const san::Config &c) {
  (void)c;
  for (int i = 0; i < rtc.nAgents; i++) {
    RtcAgent &r = rtc.agents[i];
    r.status = 255; r.stale = 0; r.progress = -1; r.voices = 0;
    for (auto &s : states)
      if (s.agent == r.slug) {
        r.voices = s.voicePending > 255 ? 255 : (uint8_t)s.voicePending;
        r.status = statusCode(s.status);
        r.stale = s.stale;
        r.progress = s.progress;
        break;
      }
  }
}

std::vector<san::AgentState> rtcLoadStates() {
  std::vector<san::AgentState> v;
  for (int i = 0; i < rtc.nAgents; i++) {
    RtcAgent &r = rtc.agents[i];
    san::AgentState s;
    s.agent = r.slug; s.name = r.name; s.status = statusName(r.status);
    s.progress = r.progress; s.stale = r.stale; s.voicePending = r.voices;
    v.push_back(s);
  }
  return v;
}

std::vector<san::PrevAgent> rtcPrevAgents() {
  std::vector<san::PrevAgent> v;
  for (int i = 0; i < rtc.nAgents; i++) {
    const RtcAgent &r = rtc.agents[i];
    san::PrevAgent p;
    p.agent = r.slug;
    p.status = statusName(r.status);  // "" si inconnu (255)
    for (int k = 0; k < r.nSeen && k < (int)san::kSeenMax; k++) p.seen.push_back(r.seen[k]);
    v.push_back(p);
  }
  return v;
}

void rtcUpdateSeen(const std::vector<san::AgentState> &states) {
  for (int i = 0; i < rtc.nAgents; i++) {
    RtcAgent &r = rtc.agents[i];
    for (auto &s : states) {
      if (s.agent != r.slug) continue;
      std::vector<uint16_t> seen(r.seen, r.seen + (r.nSeen < san::kSeenMax ? r.nSeen : san::kSeenMax));
      san::updateSeen(seen, s.voiceClips);
      r.nSeen = (uint8_t)seen.size();
      for (size_t k = 0; k < seen.size(); k++) r.seen[k] = seen[k];
      break;
    }
  }
}
