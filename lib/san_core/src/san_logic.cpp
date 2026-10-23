#include "san_logic.h"

namespace san {

const char *phaseName(Phase p) {
  switch (p) {
    case Phase::Boot: return "BOOT";
    case Phase::Register: return "REGISTER";
    case Phase::Pairing: return "PAIRING";
    case Phase::Run: return "RUN";
  }
  return "?";
}

Phase phaseAfterBoot(bool hasToken, bool wasActive) {
  if (!hasToken) return Phase::Register;
  return wasActive ? Phase::Run : Phase::Pairing;
}
Phase phaseOnRegistered() { return Phase::Pairing; }
Phase phaseOnStatus(Phase cur, DeviceStatus st) {
  if (st == DeviceStatus::Active) return Phase::Run;
  if (st == DeviceStatus::Pending) return Phase::Pairing;
  return cur;
}
Phase phaseOnHttp(Phase cur, int code) {
  if (code == 401) return Phase::Register;
  if (code == 403 && cur == Phase::Run) return Phase::Pairing;
  return cur;
}
bool httpCodeClearsToken(int code) { return code == 401; }

int64_t advanceAck(int64_t current, int64_t candidate) { return candidate > current ? candidate : current; }

int64_t ackAfterProcessing(int64_t current, const std::vector<EventItem> &events, size_t n) {
  int64_t ack = current;
  for (size_t i = 0; i < n && i < events.size(); i++) ack = advanceAck(ack, events[i].id);
  return ack;
}

bool isMuted(const Config &cfg, const std::string &agent) {
  const AgentCfg *a = cfg.find(agent);
  return a && a->muted;
}

bool isNotableStatus(const std::string &s) { return s == "waiting" || s == "error" || s == "done"; }

ScreenDecision decideScreen(const Config &cfg, const std::vector<PrevStatus> &prev,
                            const std::vector<AgentState> &states, const std::vector<EventItem> &events) {
  ScreenDecision d;
  for (auto &e : events) {
    if (isMuted(cfg, e.agent)) continue;
    d.alertEvents++;
    d.screen = true;
    d.sound = true;
    if (e.isHigh()) d.highPriority = true;
  }
  for (auto &s : states) {
    if (isMuted(cfg, s.agent) || s.stale || !isNotableStatus(s.status)) continue;
    // Un agent absent de la config manuelle (non suivi) ne declenche rien.
    const AgentCfg *c = cfg.find(s.agent);
    if (c && !c->subscribed) continue;
    bool changed = true;
    for (auto &p : prev) if (p.agent == s.agent) { changed = (p.status != s.status); break; }
    if (changed) d.screen = true;
  }
  return d;
}

std::vector<AgentState> orderStates(const Config &cfg, const std::vector<AgentState> &states) {
  std::vector<AgentState> out;
  std::vector<bool> used(states.size(), false);
  for (auto &a : cfg.agents) {
    if (!a.subscribed) continue;
    for (size_t i = 0; i < states.size(); i++)
      if (!used[i] && states[i].agent == a.agent) { out.push_back(states[i]); used[i] = true; break; }
  }
  if (cfg.agents.empty())
    for (size_t i = 0; i < states.size(); i++) out.push_back(states[i]);
  return out;
}

size_t utf8Length(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
  return n;
}

std::string truncateChars(const std::string &s, int maxChars) {
  if (maxChars <= 0 || (int)utf8Length(s) <= maxChars) return s;
  bool ell = maxChars > 3;
  int keep = ell ? maxChars - 3 : maxChars;
  size_t i = 0;
  int count = 0;
  while (i < s.size()) {
    if (((unsigned char)s[i] & 0xC0) != 0x80) {
      if (count == keep) break;
      count++;
    }
    i++;
  }
  return s.substr(0, i) + (ell ? "..." : "");
}

int computeSleepSeconds(int nextSyncS, int failures) {
  int base = nextSyncS > 0 ? nextSyncS : 60;
  if (base < 10) base = 10;
  if (base > 3600) base = 3600;
  if (failures > 0) {
    int f = failures > 4 ? 4 : failures;
    int backoff = 60 << f;  // 120, 240, 480, 960
    if (backoff > 900) backoff = 900;
    if (backoff > base) base = backoff;
  }
  return base;
}

void toggleSubscribed(Config &c, size_t i) {
  if (i < c.agents.size() && c.mode != "auto") {
    c.agents[i].subscribed = !c.agents[i].subscribed;
    c.agents[i].isNew = false;
  }
}
void toggleMuted(Config &c, size_t i) {
  if (i < c.agents.size()) c.agents[i].muted = !c.agents[i].muted;
}

bool configDiffers(const Config &a, const Config &b) {
  if (a.mode != b.mode || a.agents.size() != b.agents.size()) return true;
  for (size_t i = 0; i < a.agents.size(); i++) {
    auto &x = a.agents[i]; auto &y = b.agents[i];
    if (x.agent != y.agent || x.subscribed != y.subscribed || x.muted != y.muted ||
        x.position != y.position || x.canSendVoice != y.canSendVoice)
      return true;
  }
  return false;
}

}  // namespace san
