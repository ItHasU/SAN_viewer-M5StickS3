#include "san_ui.h"

namespace san {

namespace {
const int kVolumes[] = {0, 64, 128, 160, 192, 230, 255};
const int kBrightness[] = {20, 40, 70, 110, 160, 210, 255};
const int kPauses[] = {10, 30, 60, 300, 0};

template <size_t N>
size_t nearest(const int (&a)[N], int v) {
  size_t best = 0;
  int bd = 1 << 30;
  for (size_t i = 0; i < N; i++) {
    int d = a[i] > v ? a[i] - v : v - a[i];
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}
template <size_t N>
int cycle(const int (&a)[N], int v) { return a[(nearest(a, v) + 1) % N]; }
}  // namespace

int nextVolume(int cur) { return cycle(kVolumes, cur); }
int nextBrightness(int cur) { return cycle(kBrightness, cur); }
int nextPause(int cur) {
  for (size_t i = 0; i < sizeof kPauses / sizeof *kPauses; i++)
    if (kPauses[i] == cur) return kPauses[(i + 1) % (sizeof kPauses / sizeof *kPauses)];
  return kDefaultPauseS;
}
int sanitizeVolume(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
int sanitizeBrightness(int v) { return v < 5 ? 5 : v > 255 ? 255 : v; }
int sanitizePause(int v) {
  for (int p : kPauses) if (p == v) return v;
  return kDefaultPauseS;
}

std::string formatVolume(int v) {
  if (v <= 0) return "Muet";
  return std::to_string((v * 100 + 127) / 255) + "%";
}
std::string formatBrightness(int v) { return std::to_string((v * 100 + 127) / 255) + "%"; }
std::string formatPause(int s) {
  if (s <= 0) return "Jamais";
  if (s < 60) return std::to_string(s) + " s";
  return std::to_string(s / 60) + " min";
}

std::string formatCount(int n) {
  if (n < 0) n = 0;
  return n > 99 ? "99+" : std::to_string(n);
}

Activity summarizeActivity(const std::vector<AgentState> &states) {
  Activity a;
  a.followed = (int)states.size();
  for (auto &s : states) if (s.status == "working" && !s.stale) a.working++;
  return a;
}

std::string pickVoiceTarget(const Config &cfg, const std::vector<AgentState> &states, const std::string &lastAgent) {
  if (!lastAgent.empty()) {
    const AgentCfg *a = cfg.find(lastAgent);
    if (a && a->canSendVoice) return a->agent;
  }
  for (auto &s : states) {
    const AgentCfg *a = cfg.find(s.agent);
    if (a && a->canSendVoice) return a->agent;
  }
  return "";
}

Scene buildScene(const std::vector<AgentState> &states, size_t maxShown) {
  Scene sc;
  if (maxShown < 2) maxShown = 2;
  size_t n = states.size(), first = 0, count = n;
  if (n > maxShown) {
    count = maxShown - 1;
    sc.more = (int)(n - count);
  }
  for (size_t i = first; i < first + count && i < n; i++) {
    IconStyle st = statusIcon(states[i].status, states[i].stale);
    bool working = st.icon == Icon::Working;
    sc.agents.push_back({st.icon, st.color, working});
    if (working) sc.calm = false;
  }
  // "+N" masque aussi des agents qui travaillent : ils comptent pour l'animation
  for (size_t i = 0; i < n; i++)
    if (!(i >= first && i < first + count) && statusIcon(states[i].status, states[i].stale).icon == Icon::Working) sc.calm = false;
  return sc;
}

int sceneBreath(uint32_t frame) {
  int p = (int)(frame % 48), tri = p < 24 ? p : 48 - p;
  return tri * 4 / 24 - 2;
}

size_t menuFirstRow(size_t cursor, size_t total, size_t visible) {
  if (total <= visible || cursor < visible) return 0;
  size_t first = cursor - visible + 1;
  return first + visible > total ? total - visible : first;
}
int resetProgress(uint32_t heldMs) { return heldMs >= kResetHoldMs ? 100 : (int)(heldMs * 100 / kResetHoldMs); }
int resetSecondsLeft(uint32_t heldMs) {
  if (heldMs >= kResetHoldMs) return 0;
  return (int)((kResetHoldMs - heldMs + 999) / 1000);
}

BatteryStyle batteryStyle(int level, Charge charge) {
  BatteryStyle b{-1, 0x7BEF, false, false};
  if (level >= 0 && level <= 100) b.level = level;
  b.bolt = charge == Charge::Charging;
  if (b.level < 0) return b;
  if (b.bolt) b.color = 0x07E0;
  else if (b.level <= 15) { b.color = 0xF800; b.low = true; }
  else if (b.level <= 40) b.color = 0xFD20;
  else b.color = 0x07E0;
  return b;
}
std::string formatBattery(int level) {
  return level < 0 || level > 100 ? "--" : std::to_string(level) + "%";
}

MainAction mainAction(Button b, Press p, bool hasNotifications) {
  if (b == Button::Top) {
    return p == Press::Click && hasNotifications ? MainAction::AckLatest : MainAction::None;
  }
  switch (p) {
    case Press::Click: return MainAction::Replay;
    case Press::Hold: return MainAction::Record;
    case Press::DoubleClick: return MainAction::OpenMenu;
  }
  return MainAction::None;
}

MenuAct menuStep(MenuState &m, MenuEvent e) {
  if (e == MenuEvent::Idle) return MenuAct::Close;
  if (e == MenuEvent::FrontClick) {
    m.cursor = (m.cursor + 1) % kMenuRootRows;
    return MenuAct::Redraw;
  }
  if (e == MenuEvent::FrontHold) return MenuAct::Close;
  if (e == MenuEvent::TopClick) {
    switch (m.item()) {
      case MenuItem::Volume: return MenuAct::VolumeNext;
      case MenuItem::Brightness: return MenuAct::BrightnessNext;
      case MenuItem::Pause: return MenuAct::PauseNext;
      case MenuItem::WifiConfig: return MenuAct::WifiConfig;
      case MenuItem::Reset: return MenuAct::AskReset;
      case MenuItem::Exit: return MenuAct::Close;
    }
  }
  return MenuAct::None;
}

}  // namespace san
