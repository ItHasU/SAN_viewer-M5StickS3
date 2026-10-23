#include "ui.h"

#include <M5Unified.h>

#include "san_logic.h"

namespace ui {

static const lgfx::IFont *kFont = &fonts::lgfxJapanGothic_12;  // accents latins inclus

static uint16_t statusColor(const std::string &s) {
  if (s == "working") return TFT_CYAN;
  if (s == "waiting") return TFT_ORANGE;
  if (s == "error") return TFT_RED;
  if (s == "done") return TFT_GREEN;
  return TFT_DARKGREY;
}

void begin() {
  M5.Display.setRotation(0);
  M5.Display.setFont(kFont);
  M5.Display.setTextWrap(true, false);
}
void backlightOn() { M5.Display.wakeup(); M5.Display.setBrightness(110); }
void backlightOff() { M5.Display.setBrightness(0); M5.Display.sleep(); }

static void header(const char *title, uint16_t color) {
  auto &d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, d.width(), 18, color);
  d.setTextColor(TFT_BLACK, color);
  d.setCursor(4, 3);
  d.print(title);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(4, 24);
}

void message(const char *title, const std::string &text, uint16_t color) {
  header(title, color == 0xFFFF ? TFT_LIGHTGREY : color);
  M5.Display.print(text.c_str());
}

void pairing(const std::string &code, const std::string &url) {
  header("Appairage", TFT_ORANGE);
  auto &d = M5.Display;
  d.println("Activez cette carte");
  d.println("dans l'interface web");
  d.setTextSize(2.4f);
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.setCursor(4, 90);
  d.print(code.c_str());
  d.setTextSize(1);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  d.setCursor(4, 170);
  d.print(url.c_str());
}

void status(const std::vector<san::AgentState> &states, const san::Config &cfg, size_t sel, int battery) {
  auto &d = M5.Display;
  char title[24];
  if (battery >= 0) snprintf(title, sizeof title, "SAN  %d%%", battery);
  else snprintf(title, sizeof title, "SAN");
  header(title, TFT_DARKCYAN);
  if (states.empty()) {
    d.println("Aucun agent suivi.");
    d.println("Appui long cote : menu");
    return;
  }
  int y = 22;
  for (size_t i = 0; i < states.size() && y < d.height() - 14; i++) {
    const auto &s = states[i];
    bool muted = san::isMuted(cfg, s.agent);
    if (i == sel) d.fillRect(0, y - 1, d.width(), 17, 0x2104);
    d.fillCircle(7, y + 7, 4, s.stale ? TFT_DARKGREY : statusColor(s.status));
    d.setTextColor(muted ? TFT_DARKGREY : TFT_WHITE, i == sel ? 0x2104 : TFT_BLACK);
    d.setCursor(16, y);
    d.print(san::truncateChars(s.name, 14).c_str());
    y += 17;
    if (i == sel) {
      d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      d.setCursor(4, y);
      std::string line = s.status + (s.stale ? " (perime)" : "");
      if (s.progress >= 0) line += " " + std::to_string(s.progress) + "%";
      d.print(line.c_str());
      y += 15;
      if (!s.detail.empty()) {
        d.setCursor(4, y);
        d.print(san::truncateChars(s.detail, 80).c_str());
        y = d.getCursorY() + 16;
      }
    }
  }
}

void alert(const std::string &agentName, const std::string &kind, const std::string &text, bool high) {
  header(kind == "voice" ? "Message vocal" : "Nudge", high ? TFT_RED : TFT_ORANGE);
  auto &d = M5.Display;
  d.setTextColor(TFT_YELLOW, TFT_BLACK);
  d.println(san::truncateChars(agentName, 20).c_str());
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.println();
  d.print(san::truncateChars(text.empty() ? "(sans texte)" : text, 120).c_str());
}

void recording(const std::string &agentName, int ms, int maxMs) {
  auto &d = M5.Display;
  header("REC vers", TFT_RED);
  d.println(san::truncateChars(agentName, 20).c_str());
  d.setCursor(4, 100);
  d.setTextSize(2);
  d.printf("%d.%ds", ms / 1000, (ms / 100) % 10);
  d.setTextSize(1);
  d.drawRect(4, 140, d.width() - 8, 10, TFT_WHITE);
  d.fillRect(5, 141, (d.width() - 10) * ms / maxMs, 8, TFT_RED);
  d.setCursor(4, 170);
  d.print("Relacher pour envoyer");
}

void menu(const san::Config &cfg, size_t cursor, const std::string &note) {
  auto &d = M5.Display;
  header(cfg.mode == "auto" ? "Config (auto)" : "Abonnements", TFT_PURPLE);
  size_t first = cursor > 8 ? cursor - 8 : 0;
  int y = 22;
  for (size_t i = first; i < cfg.agents.size() && y < d.height() - 40; i++) {
    const auto &a = cfg.agents[i];
    uint16_t bg = i == cursor ? 0x2104 : TFT_BLACK;
    d.fillRect(0, y - 1, d.width(), 17, bg);
    d.setTextColor(a.subscribed ? TFT_GREEN : TFT_DARKGREY, bg);
    d.setCursor(2, y);
    d.print(a.subscribed ? "[x]" : "[ ]");
    d.setTextColor(a.muted ? TFT_DARKGREY : TFT_WHITE, bg);
    d.setCursor(26, y);
    std::string n = san::truncateChars(a.name, 11);
    if (a.isNew) n += "*";
    d.print(n.c_str());
    if (a.muted) { d.setTextColor(TFT_ORANGE, bg); d.setCursor(d.width() - 14, y); d.print("M"); }
    y += 17;
  }
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(2, d.height() - 38);
  d.print("A:suiv  A+:muet\nB:cocher  B+:OK");
  if (!note.empty()) { d.setTextColor(TFT_YELLOW, TFT_BLACK); d.setCursor(2, d.height() - 66); d.print(note.c_str()); }
}

}  // namespace ui
