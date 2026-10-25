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
  s.volume = san::sanitizeVolume(p.getInt("vol", san::kDefaultVolume));
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
  p.putInt("vol", s.volume);
  p.putInt("bri", s.brightness);
  p.putInt("pause", s.pauseS);
  p.end();
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
static const uint32_t kMagic = 0x53414E04;
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
    r.flags = (a.subscribed ? 1 : 0) | (a.isNew ? 2 : 0) | (a.muted ? 4 : 0) | (a.canSendVoice ? 8 : 0);
    r.status = 255;
    for (int i = 0; i < oldN; i++)
      if (!strcmp(old[i].slug, r.slug)) {
        r.status = old[i].status; r.stale = old[i].stale; r.progress = old[i].progress;
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
    a.subscribed = rtc.agents[i].flags & 1;
    a.isNew = rtc.agents[i].flags & 2;
    a.muted = rtc.agents[i].flags & 4;
    a.canSendVoice = rtc.agents[i].flags & 8;
    c.agents.push_back(a);
  }
  return c;
}

void rtcStoreStates(const std::vector<san::AgentState> &states, const san::Config &c) {
  (void)c;
  for (int i = 0; i < rtc.nAgents; i++) {
    RtcAgent &r = rtc.agents[i];
    r.status = 255; r.stale = 0; r.progress = -1;
    for (auto &s : states)
      if (s.agent == r.slug) {
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
    s.progress = r.progress; s.stale = r.stale;
    v.push_back(s);
  }
  return v;
}

std::vector<san::PrevStatus> rtcPrevStatuses() {
  std::vector<san::PrevStatus> v;
  for (int i = 0; i < rtc.nAgents; i++)
    if (rtc.agents[i].status != 255) v.push_back({rtc.agents[i].slug, statusName(rtc.agents[i].status)});
  return v;
}

san::NotifQueue rtcLoadNotifs() {
  std::vector<san::Notif> v;
  for (int i = 0; i < rtc.nNotifs && i < (int)san::NotifQueue::kCapacity; i++) {
    const RtcNotif &r = rtc.notifs[i];
    san::Notif n;
    n.agent = r.slug;
    n.type = (san::NudgeType)(r.type <= (uint8_t)san::NudgeType::Error ? r.type : (uint8_t)san::NudgeType::Notification);
    n.voice = r.flags & 1;
    n.high = r.flags & 2;
    n.count = r.count ? r.count : 1;
    n.id = r.id;
    v.push_back(n);
  }
  san::NotifQueue q;
  q.restore(v);
  return q;
}

void rtcStoreNotifs(const san::NotifQueue &q) {
  memset(rtc.notifs, 0, sizeof rtc.notifs);
  int n = 0;
  for (auto &it : q.items()) {
    if (it.sys != san::Sys::None) continue;  // etat de connexion : ephemere (les compteurs d'echec, eux, sont en RTC)
    if (n >= (int)san::NotifQueue::kCapacity) break;
    RtcNotif &r = rtc.notifs[n++];
    cpy(r.slug, sizeof r.slug, it.agent);
    r.type = (uint8_t)it.type;
    r.flags = (it.voice ? 1 : 0) | (it.high ? 2 : 0);
    r.count = it.count > 255 ? 255 : (uint8_t)it.count;
    r.id = it.id;
  }
  rtc.nNotifs = n;
}
