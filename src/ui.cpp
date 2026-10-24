#include "ui.h"

#include <M5Unified.h>

#include "san_logic.h"

// Ecran paysage 240x135. Pictogrammes en primitives M5GFX ; texte reduit a l'essentiel, grosse police.
namespace ui {

static const lgfx::IFont *kFont24 = &fonts::lgfxJapanGothic_24;  // accents latins inclus
static const lgfx::IFont *kFont20 = &fonts::lgfxJapanGothic_20;
static const lgfx::IFont *kFont40 = &fonts::lgfxJapanGothic_40;

static const uint16_t kSelBg = 0x39E7;
static const uint16_t kVoiceBg = 0x041F;  // bleu : message vocal

// Fond sombre teinte pour un etat (1/4 de la luminosite de la couleur).
static uint16_t dim(uint16_t c) {
  return (uint16_t)((((c >> 11) & 31) >> 2) << 11 | (((c >> 5) & 63) >> 2) << 5 | ((c & 31) >> 2));
}

void begin() {
  M5.Display.setRotation(3);  // paysage ; bouton A (face avant) a droite, USB-C a gauche
  M5.Display.setFont(kFont24);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(true, false);
}
void backlightOn() { M5.Display.wakeup(); M5.Display.setBrightness(110); }
void backlightOff() { M5.Display.setBrightness(0); M5.Display.sleep(); }

// ---------------------------------------------------------------- pictogrammes
// Tous centres en (cx,cy), rayon r (demi-taille), couleur fg ; bg = couleur de fond pour les "trous".
static void wide(int x0, int y0, int x1, int y1, float w, uint16_t c) {
  M5.Display.drawWideLine(x0, y0, x1, y1, w, c);
}

static void iconGear(int cx, int cy, int r, uint16_t fg, uint16_t bg) {
  auto &d = M5.Display;
  d.fillCircle(cx, cy, r * 62 / 100, fg);
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785398f;
    d.fillCircle(cx + (int)(cosf(a) * r * 0.78f), cy + (int)(sinf(a) * r * 0.78f), r / 5 + 1, fg);
  }
  d.fillCircle(cx, cy, r / 4, bg);
}

static void iconExclaim(int cx, int cy, int r, uint16_t fg) {
  auto &d = M5.Display;
  d.fillRoundRect(cx - r * 15 / 100, cy - r * 85 / 100, r * 30 / 100 + 1, r * 100 / 100, r / 10 + 1, fg);
  d.fillCircle(cx, cy + r * 65 / 100, r * 18 / 100 + 1, fg);
}

static void iconBell(int cx, int cy, int r, uint16_t fg) {
  auto &d = M5.Display;
  d.fillCircle(cx, cy - r * 15 / 100, r * 50 / 100, fg);
  int x1 = cx - r * 50 / 100, x2 = cx + r * 50 / 100, y1 = cy - r * 10 / 100;
  int x3 = cx + r * 85 / 100, x4 = cx - r * 85 / 100, y2 = cy + r * 48 / 100;
  d.fillTriangle(x1, y1, x2, y1, x3, y2, fg);
  d.fillTriangle(x1, y1, x3, y2, x4, y2, fg);
  d.fillCircle(cx, cy - r * 75 / 100, r * 12 / 100 + 1, fg);
  d.fillCircle(cx, cy + r * 66 / 100, r * 20 / 100 + 1, fg);
}

static void iconCheck(int cx, int cy, int r, uint16_t fg) {
  float w = r * 0.16f + 1;
  wide(cx - r * 65 / 100, cy + r * 5 / 100, cx - r * 18 / 100, cy + r * 52 / 100, w, fg);
  wide(cx - r * 18 / 100, cy + r * 52 / 100, cx + r * 70 / 100, cy - r * 48 / 100, w, fg);
}

static void iconCross(int cx, int cy, int r, uint16_t fg) {
  float w = r * 0.16f + 1;
  wide(cx - r * 55 / 100, cy - r * 55 / 100, cx + r * 55 / 100, cy + r * 55 / 100, w, fg);
  wide(cx - r * 55 / 100, cy + r * 55 / 100, cx + r * 55 / 100, cy - r * 55 / 100, w, fg);
}

static void iconMoon(int cx, int cy, int r, uint16_t fg, uint16_t bg) {
  auto &d = M5.Display;
  d.fillCircle(cx, cy, r * 70 / 100, fg);
  d.fillCircle(cx + r * 35 / 100, cy - r * 25 / 100, r * 60 / 100, bg);
}

static void iconDash(int cx, int cy, int r, uint16_t fg) {
  M5.Display.fillRoundRect(cx - r * 60 / 100, cy - r * 10 / 100, r * 120 / 100, r * 20 / 100 + 2, r / 10 + 1, fg);
}

static void iconSpeaker(int cx, int cy, int r, uint16_t fg, bool waves) {
  auto &d = M5.Display;
  d.fillRect(cx - r * 70 / 100, cy - r * 25 / 100, r * 35 / 100 + 1, r * 50 / 100 + 1, fg);
  d.fillTriangle(cx - r * 35 / 100, cy - r * 25 / 100, cx - r * 35 / 100, cy + r * 25 / 100, cx + r * 5 / 100, cy + r * 65 / 100, fg);
  d.fillTriangle(cx - r * 35 / 100, cy - r * 25 / 100, cx + r * 5 / 100, cy - r * 65 / 100, cx + r * 5 / 100, cy + r * 65 / 100, fg);
  d.fillTriangle(cx - r * 35 / 100, cy - r * 25 / 100, cx + r * 5 / 100, cy + r * 65 / 100, cx + r * 5 / 100, cy - r * 65 / 100, fg);
  if (waves) {
    d.fillArc(cx + r * 5 / 100, cy, r * 45 / 100, r * 60 / 100, 315, 45, fg);
    d.fillArc(cx + r * 5 / 100, cy, r * 75 / 100, r * 90 / 100, 315, 45, fg);
  }
}

static void iconSpeakerMuted(int cx, int cy, int r, uint16_t fg, uint16_t slash) {
  iconSpeaker(cx - r / 8, cy, r, fg, false);
  wide(cx - r * 50 / 100, cy + r * 80 / 100, cx + r * 80 / 100, cy - r * 80 / 100, r * 0.12f + 1, slash);
}

static void iconMic(int cx, int cy, int r, uint16_t fg) {
  auto &d = M5.Display;
  d.fillRoundRect(cx - r * 25 / 100, cy - r * 85 / 100, r * 50 / 100, r * 90 / 100, r * 25 / 100, fg);
  d.fillArc(cx, cy - r * 5 / 100, r * 44 / 100, r * 58 / 100, 0, 180, fg);
  d.fillRect(cx - r * 6 / 100, cy + r * 50 / 100, r * 12 / 100 + 1, r * 25 / 100, fg);
  d.fillRect(cx - r * 30 / 100, cy + r * 75 / 100, r * 60 / 100, r * 12 / 100 + 1, fg);
}

static void iconWifi(int cx, int cy, int r, uint16_t fg, bool crossed = false) {
  auto &d = M5.Display;
  int oy = cy + r * 55 / 100;  // centre des arcs (point du bas)
  for (int k = 1; k <= 3; k++) d.fillArc(cx, oy, r * k * 30 / 100 + r * 8 / 100, r * k * 30 / 100 + r * 22 / 100 + 1, 225, 315, fg);
  d.fillCircle(cx, oy, r * 10 / 100 + 1, fg);
  if (crossed) wide(cx - r * 70 / 100, cy - r * 60 / 100, cx + r * 70 / 100, cy + r * 80 / 100, r * 0.1f + 1, TFT_RED);
}

static void iconBattery(int x, int y, int w, int h, int pct) {  // coin haut-gauche
  auto &d = M5.Display;
  uint16_t c = pct < 0 ? TFT_DARKGREY : pct <= 15 ? TFT_RED : pct <= 40 ? TFT_ORANGE : TFT_GREEN;
  d.fillRect(x + w, y + h / 4, 3, h / 2, TFT_WHITE);
  d.drawRoundRect(x, y, w, h, 3, TFT_WHITE);
  if (pct >= 0) {
    int fw = (w - 4) * (pct > 100 ? 100 : pct) / 100;
    if (fw < 2 && pct >= 0) fw = 2;
    d.fillRect(x + 2, y + 2, fw, h - 4, c);
  }
}

static void iconStatus(san::Icon ic, int cx, int cy, int r, uint16_t fg, uint16_t bg) {
  switch (ic) {
    case san::Icon::Working: iconGear(cx, cy, r, fg, bg); break;
    case san::Icon::Waiting: iconExclaim(cx, cy, r, fg); break;
    case san::Icon::Done: iconCheck(cx, cy, r, fg); break;
    case san::Icon::Error: iconCross(cx, cy, r, fg); break;
    case san::Icon::Stale: iconDash(cx, cy, r, fg); break;
    default: iconMoon(cx, cy, r, fg, bg); break;
  }
}

// Icone choisie d'apres la couleur d'un message (convention des appels de ui::message).
static void iconForColor(uint16_t color, int cx, int cy, int r, uint16_t fg, uint16_t bg) {
  if (color == TFT_GREEN) iconCheck(cx, cy, r, fg);
  else if (color == TFT_RED) iconCross(cx, cy, r, fg);
  else if (color == TFT_ORANGE) iconExclaim(cx, cy, r, fg);
  else if (color == TFT_CYAN) iconGear(cx, cy, r, fg, bg);
  else if (color == TFT_YELLOW) iconWifi(cx, cy, r, fg);
  else iconMoon(cx, cy, r, fg, bg);
}

// ---------------------------------------------------------------- ecrans
static void textStyle(const lgfx::IFont *f, uint16_t fg, uint16_t bg) {
  auto &d = M5.Display;
  d.setFont(f);
  d.setTextSize(1);
  d.setTextColor(fg, bg);
}

void message(const char *title, const std::string &text, uint16_t color) {
  auto &d = M5.Display;
  if (color == 0xFFFF) color = TFT_LIGHTGREY;
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, d.width(), 44, color);
  iconForColor(color, 24, 22, 17, TFT_BLACK, color);
  textStyle(kFont24, TFT_BLACK, color);
  d.setCursor(52, 10);
  d.print(san::truncateChars(title, 12).c_str());
  textStyle(kFont24, TFT_WHITE, TFT_BLACK);
  d.setCursor(4, 50);
  d.print(san::truncateChars(text, 54).c_str());
}

void pairing(const std::string &code, const std::string &url) {
  auto &d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, d.width(), 36, TFT_ORANGE);
  iconExclaim(24, 18, 15, TFT_BLACK);
  iconWifi(68, 18, 17, TFT_BLACK);
  textStyle(kFont40, TFT_YELLOW, TFT_BLACK);
  d.setTextDatum(top_center);
  d.drawString(code.c_str(), d.width() / 2, 44);
  d.setTextDatum(top_left);
  textStyle(kFont20, TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(4, 100);
  d.print(san::truncateChars(url, 40).c_str());
}

void status(const std::vector<san::AgentState> &states, const san::Config &cfg, size_t sel, int battery) {
  auto &d = M5.Display;
  const int W = d.width();
  if (states.empty()) {
    d.fillScreen(TFT_BLACK);
    iconMoon(52, 52, 40, TFT_DARKGREY, TFT_BLACK);
    iconBattery(W - 40, 6, 32, 16, battery);
    textStyle(kFont24, TFT_LIGHTGREY, TFT_BLACK);
    d.setCursor(4, 104);
    d.print("B long : menu");
    return;
  }
  if (sel >= states.size()) sel = 0;
  const auto &s = states[sel];
  san::IconStyle st = san::statusIcon(s.status, s.stale);
  uint16_t bg = dim(st.color);
  d.fillScreen(bg);
  iconStatus(st.icon, 52, 52, 40, st.color, bg);
  // nom d'agent court
  textStyle(kFont24, TFT_WHITE, bg);
  d.setCursor(104, 36);
  d.print(san::truncateChars(s.name, 10).c_str());
  // progression : barre
  if (s.progress >= 0) {
    int bw = W - 104 - 8, p = s.progress > 100 ? 100 : s.progress;
    d.drawRoundRect(104, 74, bw, 16, 4, TFT_WHITE);
    d.fillRoundRect(106, 76, (bw - 4) * p / 100, 12, 3, st.color);
  }
  // coin haut-droit : muet + batterie
  iconBattery(W - 40, 6, 32, 16, battery);
  if (san::isMuted(cfg, s.agent)) iconSpeakerMuted(W - 62, 14, 11, TFT_LIGHTGREY, TFT_RED);
  // bande basse : un point par agent
  size_t n = states.size();
  int step = n > 1 ? (W - 30) / (int)n : 0;
  if (step > 26) step = 26;
  int x0 = W / 2 - (int)(n - 1) * step / 2;
  for (size_t i = 0; i < n; i++) {
    san::IconStyle si = san::statusIcon(states[i].status, states[i].stale);
    int x = x0 + (int)i * step, y = 118;
    d.fillCircle(x, y, 7, si.color);
    if (i == sel) d.drawCircle(x, y, 11, TFT_WHITE), d.drawCircle(x, y, 10, TFT_WHITE);
  }
}

void alert(const std::string &agentName, const std::string &kind, const std::string &text, bool high) {
  (void)text;  // notifier, pas lire : le texte du nudge n'est pas affiche
  auto &d = M5.Display;
  bool voice = kind == "voice";
  uint16_t bg = voice ? kVoiceBg : (high ? TFT_RED : TFT_ORANGE);
  uint16_t fg = (high && !voice) ? TFT_WHITE : TFT_BLACK;
  if (voice) fg = TFT_WHITE;
  d.fillScreen(bg);
  if (voice) iconSpeaker(62, 56, 52, fg, true);
  else iconBell(62, 56, 52, fg);
  if (high && !voice) {  // priorite haute : double point d'exclamation
    iconExclaim(160, 52, 34, fg);
    iconExclaim(200, 52, 34, fg);
  } else if (!voice) {
    iconExclaim(180, 52, 34, fg);
  }
  textStyle(kFont24, fg, bg);
  d.setTextDatum(top_center);
  d.drawString(san::truncateChars(agentName, 18).c_str(), d.width() / 2, 104);
  d.setTextDatum(top_left);
}

void recording(const std::string &agentName, int ms, int maxMs) {
  auto &d = M5.Display;
  uint16_t bg = dim(TFT_RED);
  d.fillScreen(bg);
  iconMic(40, 44, 34, TFT_WHITE);
  d.fillCircle(d.width() - 20, 20, 10, TFT_RED);
  textStyle(kFont40, TFT_WHITE, bg);
  d.setCursor(84, 22);
  d.printf("%d.%ds", ms / 1000, (ms / 100) % 10);
  textStyle(kFont24, TFT_LIGHTGREY, bg);
  d.setCursor(4, 74);
  d.print(san::truncateChars(agentName, 18).c_str());
  d.drawRoundRect(4, 108, d.width() - 8, 20, 5, TFT_WHITE);
  if (maxMs > 0) d.fillRoundRect(6, 110, (d.width() - 12) * (ms > maxMs ? maxMs : ms) / maxMs, 16, 4, TFT_RED);
}

void menu(const san::Config &cfg, size_t cursor, const std::string &note) {
  auto &d = M5.Display;
  const int W = d.width(), rowH = 34, rows = 3;
  d.fillScreen(TFT_BLACK);
  size_t first = cursor >= (size_t)rows ? cursor - rows + 1 : 0;
  int y = 0;
  for (size_t i = first; i < cfg.agents.size() && i < first + rows; i++) {
    const auto &a = cfg.agents[i];
    bool cur = i == cursor;
    uint16_t bg = cur ? kSelBg : TFT_BLACK;
    d.fillRect(0, y, W, rowH - 2, bg);
    if (cur) d.fillRect(0, y, 4, rowH - 2, TFT_PURPLE);
    int cy = y + rowH / 2 - 1;
    if (a.subscribed) {
      d.fillCircle(24, cy, 12, TFT_GREEN);
      iconCheck(24, cy, 12, TFT_BLACK);
    } else {
      d.drawCircle(24, cy, 12, TFT_DARKGREY);
      d.drawCircle(24, cy, 11, TFT_DARKGREY);
    }
    textStyle(kFont24, a.muted ? TFT_DARKGREY : TFT_WHITE, bg);
    d.setCursor(46, y + 4);
    std::string n = san::truncateChars(a.name, 10);
    if (a.isNew) n += "*";
    d.print(n.c_str());
    if (a.muted) iconSpeakerMuted(W - 20, cy, 12, TFT_LIGHTGREY, TFT_RED);
    y += rowH;
  }
  if (!note.empty()) {
    textStyle(kFont24, TFT_YELLOW, TFT_BLACK);
    d.setCursor(4, 108);
    d.print(san::truncateChars(note, 18).c_str());
  } else {
    textStyle(kFont20, TFT_LIGHTGREY, TFT_BLACK);
    d.setCursor(2, 110);
    d.print("A suiv/muet B coche/ok");
  }
}

}  // namespace ui
