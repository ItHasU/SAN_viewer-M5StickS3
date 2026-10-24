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

size_t clampAgentIndex(size_t cur, size_t count) { return cur < count ? cur : 0; }
size_t nextAgentIndex(size_t cur, size_t count) {
  if (count == 0) return 0;
  return (clampAgentIndex(cur, count) + 1) % count;
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
    if (p == Press::Click) return hasNotifications ? MainAction::AckLatest : MainAction::NextAgent;
    if (p == Press::Hold) return MainAction::NextAgent;
    return MainAction::None;
  }
  switch (p) {
    case Press::Click: return MainAction::Replay;
    case Press::Hold: return MainAction::Record;
    case Press::DoubleClick: return MainAction::OpenMenu;
  }
  return MainAction::None;
}

void menuEnterAgents(MenuState &m, size_t agentCount) {
  m.rootCursor = m.cursor;
  m.screen = MenuScreen::Agents;
  m.agentCount = agentCount;
  m.cursor = 0;
}
void menuBackToRoot(MenuState &m) {
  m.screen = MenuScreen::Root;
  m.cursor = m.rootCursor;
}

MenuAct menuStep(MenuState &m, MenuEvent e) {
  if (e == MenuEvent::Idle) return MenuAct::Close;
  if (e == MenuEvent::FrontClick) {
    m.cursor = (m.cursor + 1) % m.rows();
    return MenuAct::Redraw;
  }
  if (m.screen == MenuScreen::Root) {
    if (e == MenuEvent::FrontHold) return MenuAct::Close;
    if (e == MenuEvent::TopClick) {
      switch (m.item()) {
        case MenuItem::Volume: return MenuAct::VolumeNext;
        case MenuItem::Brightness: return MenuAct::BrightnessNext;
        case MenuItem::Pause: return MenuAct::PauseNext;
        case MenuItem::Agents: return MenuAct::OpenAgents;
        case MenuItem::Exit: return MenuAct::Close;
      }
    }
    return MenuAct::None;
  }
  // Ecran agents
  if (e == MenuEvent::FrontHold) { menuBackToRoot(m); return MenuAct::Redraw; }
  if (e == MenuEvent::TopClick) return m.onSaveRow() ? MenuAct::SaveAgents : MenuAct::ToggleAgent;
  if (e == MenuEvent::TopHold) return m.onSaveRow() ? MenuAct::None : MenuAct::ToggleMute;
  return MenuAct::None;
}

}  // namespace san
