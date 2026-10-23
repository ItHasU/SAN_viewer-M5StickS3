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
  s.screenTimeoutS = p.getInt("scr", cfg::kScreenTimeoutDefaultS);
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
  p.putInt("scr", s.screenTimeoutS);
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
static const uint32_t kMagic = 0x53414E02;
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
        memcpy(r.detail, old[i].detail, sizeof r.detail);
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
    r.status = 255; r.stale = 0; r.progress = -1; r.detail[0] = 0;
    for (auto &s : states)
      if (s.agent == r.slug) {
        r.status = statusCode(s.status);
        r.stale = s.stale;
        r.progress = s.progress;
        cpy(r.detail, sizeof r.detail, s.detail);
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
    s.detail = r.detail; s.progress = r.progress; s.stale = r.stale;
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
