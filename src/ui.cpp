#include "ui.h"

#include <M5Unified.h>

#include "san_logic.h"
#include "san_sound.h"
#include "san_ui.h"
#include "config.h"

// Ecran paysage 240x135. Pictogrammes en primitives M5GFX ; texte reduit a l'essentiel, grosse police.
namespace ui {

static const lgfx::IFont *kFont24 = &fonts::lgfxJapanGothic_24;  // accents latins inclus
static const lgfx::IFont *kFont20 = &fonts::lgfxJapanGothic_20;
static const lgfx::IFont *kFont40 = &fonts::lgfxJapanGothic_40;

static const uint16_t kSelBg = 0x39E7;
static const uint16_t kVoiceBg = 0x041F;  // bleu : message vocal

static lgfx::LovyanGFX *T = &M5.Display;  // cible de dessin (ecran ou canvas hors-ecran)
static int gBrightness = 110;
static bool gAnim = false;  // animation "calcul en cours" active (voir animTick)

// Fond sombre teinte pour un etat (1/4 de la luminosite de la couleur).
static uint16_t dim(uint16_t c) {
  return (uint16_t)((((c >> 11) & 31) >> 2) << 11 | (((c >> 5) & 63) >> 2) << 5 | ((c & 31) >> 2));
}

void begin() {
  M5.Display.setRotation(1);  // paysage 240x135, USB-C en bas ; bouton lateral en haut, bouton frontal a droite
  M5.Display.setFont(kFont24);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(true, false);
}
void setBrightness(int b) {
  gBrightness = b;
  M5.Display.setBrightness(b);
}
void backlightOn() { M5.Display.wakeup(); M5.Display.setBrightness(gBrightness); }
void backlightOff() { M5.Display.setBrightness(0); M5.Display.sleep(); }

// ---------------------------------------------------------------- pictogrammes
// Tous centres en (cx,cy), rayon r (demi-taille), couleur fg ; bg = couleur de fond pour les "trous".
static void wide(int x0, int y0, int x1, int y1, float w, uint16_t c) {
  (*T).drawWideLine(x0, y0, x1, y1, w, c);
}

static void iconGear(lgfx::LovyanGFX &d, int cx, int cy, int r, uint16_t fg, uint16_t bg, float rot = 0) {
  d.fillCircle(cx, cy, r * 62 / 100, fg);
  for (int k = 0; k < 8; k++) {
    float a = rot + k * 0.785398f;
    d.fillCircle(cx + (int)(cosf(a) * r * 0.78f), cy + (int)(sinf(a) * r * 0.78f), r / 5 + 1, fg);
  }
  d.fillCircle(cx, cy, r / 4, bg);
}

static void iconExclaim(int cx, int cy, int r, uint16_t fg) {
  auto &d = (*T);
  d.fillRoundRect(cx - r * 15 / 100, cy - r * 85 / 100, r * 30 / 100 + 1, r * 100 / 100, r / 10 + 1, fg);
  d.fillCircle(cx, cy + r * 65 / 100, r * 18 / 100 + 1, fg);
}

static void iconBell(int cx, int cy, int r, uint16_t fg) {
  auto &d = (*T);
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

static void iconPlay(int cx, int cy, int r, uint16_t fg) {
  (*T).fillTriangle(cx - r * 45 / 100, cy - r * 70 / 100, cx - r * 45 / 100, cy + r * 70 / 100, cx + r * 75 / 100, cy, fg);
}

static void iconQuestion(int cx, int cy, int r, uint16_t fg) {
  auto &d = (*T);
  int ay = cy - r * 30 / 100;  // centre du crochet
  d.fillArc(cx, ay, r * 28 / 100, r * 50 / 100, 160, 80, fg);  // du bas-gauche, par le haut, jusqu'au centre-bas
  d.fillRoundRect(cx - r * 9 / 100, ay + r * 20 / 100, r * 18 / 100 + 1, r * 28 / 100, 2, fg);
  d.fillCircle(cx, cy + r * 72 / 100, r * 13 / 100 + 1, fg);
}

static void iconMoon(int cx, int cy, int r, uint16_t fg, uint16_t bg) {
  auto &d = (*T);
  d.fillCircle(cx, cy, r * 70 / 100, fg);
  d.fillCircle(cx + r * 35 / 100, cy - r * 25 / 100, r * 60 / 100, bg);
}

static void iconDash(int cx, int cy, int r, uint16_t fg) {
  (*T).fillRoundRect(cx - r * 60 / 100, cy - r * 10 / 100, r * 120 / 100, r * 20 / 100 + 2, r / 10 + 1, fg);
}

static void iconSpeaker(int cx, int cy, int r, uint16_t fg, bool waves) {
  auto &d = (*T);
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
  auto &d = (*T);
  d.fillRoundRect(cx - r * 25 / 100, cy - r * 85 / 100, r * 50 / 100, r * 90 / 100, r * 25 / 100, fg);
  d.fillArc(cx, cy - r * 5 / 100, r * 44 / 100, r * 58 / 100, 0, 180, fg);
  d.fillRect(cx - r * 6 / 100, cy + r * 50 / 100, r * 12 / 100 + 1, r * 25 / 100, fg);
  d.fillRect(cx - r * 30 / 100, cy + r * 75 / 100, r * 60 / 100, r * 12 / 100 + 1, fg);
}

static void iconWifi(int cx, int cy, int r, uint16_t fg, bool crossed = false) {
  auto &d = (*T);
  int oy = cy + r * 55 / 100;  // centre des arcs (point du bas)
  for (int k = 1; k <= 3; k++) d.fillArc(cx, oy, r * k * 30 / 100 + r * 8 / 100, r * k * 30 / 100 + r * 22 / 100 + 1, 225, 315, fg);
  d.fillCircle(cx, oy, r * 10 / 100 + 1, fg);
  if (crossed) wide(cx - r * 70 / 100, cy - r * 60 / 100, cx + r * 70 / 100, cy + r * 80 / 100, r * 0.1f + 1, TFT_RED);
}

[[maybe_unused]] static void iconBattery(int x, int y, int w, int h, int pct) {  // coin haut-gauche
  auto &d = (*T);
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
    case san::Icon::Working: iconGear((*T), cx, cy, r, fg, bg); break;
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
  else if (color == TFT_CYAN) iconGear((*T), cx, cy, r, fg, bg);
  else if (color == TFT_YELLOW) iconWifi(cx, cy, r, fg);
  else iconMoon(cx, cy, r, fg, bg);
}

// ---------------------------------------------------------------- ecrans
static void textStyle(const lgfx::IFont *f, uint16_t fg, uint16_t bg) {
  auto &d = (*T);
  d.setFont(f);
  d.setTextSize(1);
  d.setTextColor(fg, bg);
}

void message(const char *title, const std::string &text, uint16_t color) {
  gAnim = false;
  auto &d = (*T);
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

void pairing(const std::string &code) {
  gAnim = false;
  auto &d = (*T);
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, d.width(), 44, TFT_ORANGE);
  iconExclaim(34, 22, 17, TFT_BLACK);
  iconWifi(80, 22, 19, TFT_BLACK);
  textStyle(kFont40, TFT_YELLOW, TFT_BLACK);
  d.setTextDatum(middle_center);
  d.drawString(code.c_str(), d.width() / 2, 92);
  d.setTextDatum(top_left);
}

// ---------------------------------------------------------------- icones supplementaires
static void iconServer(int cx, int cy, int r, uint16_t fg, bool ok) {
  auto &d = *T;
  int w = r * 160 / 100, h = r * 70 / 100;
  for (int k = 0; k < 2; k++) {
    int y = cy - r * 85 / 100 + k * (h + 3);
    d.fillRoundRect(cx - w / 2, y, w, h, 3, fg);
    d.fillCircle(cx + w / 2 - 6, y + h / 2, 2, TFT_BLACK);
  }
  if (!ok) wide(cx - r * 80 / 100, cy + r * 80 / 100, cx + r * 80 / 100, cy - r * 80 / 100, r * 0.14f + 1, TFT_RED);
}

static void iconBolt(int cx, int cy, int r, uint16_t fg) {
  auto &d = *T;
  d.fillTriangle(cx + r * 20 / 100, cy - r, cx - r * 60 / 100, cy + r * 10 / 100, cx + r * 5 / 100, cy + r * 10 / 100, fg);
  d.fillTriangle(cx - r * 20 / 100, cy + r, cx + r * 60 / 100, cy - r * 10 / 100, cx - r * 5 / 100, cy - r * 10 / 100, fg);
}

static void iconBatteryEx(int x, int y, int w, int h, const san::BatteryStyle &b) {  // coin haut-gauche
  auto &d = *T;
  d.fillRect(x + w, y + h / 4, 3, h / 2, TFT_WHITE);
  d.drawRoundRect(x, y, w, h, 3, TFT_WHITE);
  if (b.level >= 0) {
    int fw = (w - 4) * b.level / 100;
    if (fw < 2) fw = 2;
    d.fillRect(x + 2, y + 2, fw, h - 4, b.color);
  }
  if (b.bolt) {  // eclair noir cerne de blanc, lisible sur la jauge
    iconBolt(x + w / 2, y + h / 2, h / 2 + 2, TFT_WHITE);
    iconBolt(x + w / 2, y + h / 2, h / 2 - 1, TFT_BLACK);
  }
}

static void iconSun(int cx, int cy, int r, uint16_t fg) {
  auto &d = *T;
  d.fillCircle(cx, cy, r * 38 / 100, fg);
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785398f;
    wide(cx + (int)(cosf(a) * r * 0.62f), cy + (int)(sinf(a) * r * 0.62f), cx + (int)(cosf(a) * r * 0.95f),
         cy + (int)(sinf(a) * r * 0.95f), r * 0.1f + 1, fg);
  }
}

static void iconBack(int cx, int cy, int r, uint16_t fg) {
  float w = r * 0.14f + 1;
  wide(cx + r * 60 / 100, cy, cx - r * 60 / 100, cy, w, fg);
  wide(cx - r * 60 / 100, cy, cx - r * 5 / 100, cy - r * 55 / 100, w, fg);
  wide(cx - r * 60 / 100, cy, cx - r * 5 / 100, cy + r * 55 / 100, w, fg);
}

static void iconNudge(san::NudgeIcon ic, int cx, int cy, int r, uint16_t fg);
static void iconServer(int cx, int cy, int r, uint16_t fg, bool ok);

// ---------------------------------------------------------------- vue principale
static M5Canvas gCv(&M5.Display);
static bool gCvTried = false;
static uint32_t gAnimLast = 0, gAnimFrame = 0, gAnimPeriod = 80;
static MainView gLast;
static bool gHaveLast = false;

// Attenue une couleur RGB565 de moitie (agents a l'arret).
static uint16_t fade(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }

// Scene sans notification : un pictogramme par agent suivi, sans aucun nom, dans la partie basse de la zone
// principale (y 26..75, sous la ligne "Pas de message" qui occupe 0..24 ; 50 px de haut).
// working : engrenage cyan qui tourne ; a l'arret : pictogramme statique attenue ; calme : respiration lente.
static void drawScene(lgfx::LovyanGFX &d, const san::Scene &sc, uint32_t frame) {
  const int W = d.width(), Y0 = 26, H = 50;
  int br = sc.calm ? san::sceneBreath(frame) : 0;
  size_t n = sc.agents.size();
  if (n == 0) {  // aucun agent suivi : lune calme
    iconMoon(W / 2, Y0 + H / 2 + br, 20, TFT_DARKGREY, TFT_BLACK);
    return;
  }
  size_t slots = n + (sc.more > 0 ? 1 : 0);
  int r = slots <= 2 ? 20 : slots <= 3 ? 19 : slots <= 4 ? 17 : 14;
  int slotW = W / (int)slots > 2 * r + 12 ? 2 * r + 12 : W / (int)slots;
  int x0 = (W - slotW * (int)slots) / 2 + slotW / 2;
  int cy = Y0 + H / 2;
  for (size_t i = 0; i < n; i++) {
    const auto &a = sc.agents[i];
    int cx = x0 + (int)i * slotW;
    int y = cy + (a.animated ? (int)(sinf(frame * 0.35f + i * 1.7f) * 2.0f) : br);
    if (a.animated) iconGear(d, cx, y, r, a.color, TFT_BLACK, frame * 0.12f + i * 0.9f);
    else if (a.icon == san::Icon::Idle || a.icon == san::Icon::Stale) iconStatus(a.icon, cx, y, r, a.color, TFT_BLACK);  // deja gris
    else iconStatus(a.icon, cx, y, r, fade(a.color), TFT_BLACK);
  }
  if (sc.more > 0) {
    textStyle(kFont24, TFT_LIGHTGREY, TFT_BLACK);
    d.setTextDatum(middle_center);
    d.drawString(("+" + std::to_string(sc.more)).c_str(), x0 + (int)n * slotW, cy + br);
    d.setTextDatum(top_left);
  }
}

static void drawMain(const MainView &v, uint32_t frame) {
  auto &d = *T;
  const int W = d.width();
  d.fillScreen(TFT_BLACK);
  // ---- zone du haut : notifications (la plus recente en premier)
  const int lineH = 25;
  size_t shown = v.notifs.size() < (size_t)cfg::kNotifLines ? v.notifs.size() : (size_t)cfg::kNotifLines;
  if (shown) {
    for (size_t i = 0; i < shown; i++) {
      const auto &n = v.notifs[i];
      int y = (int)i * lineH, cy = y + lineH / 2;
      san::NudgeStyle ns = san::nudgeStyle(n.type);
      uint16_t bg = n.voice ? dim(kVoiceBg) : (n.high ? ns.color : dim(ns.color));
      uint16_t fg = n.voice ? TFT_WHITE : (n.high ? ((n.type == san::NudgeType::Notification || n.type == san::NudgeType::Done) ? TFT_BLACK : TFT_WHITE) : ns.accent);
      d.fillRect(0, y, W, lineH - 1, bg);
      if (n.sys != san::Sys::None) {  // etat de connexion : icone Wi-Fi / serveur (pulse en cours, barre/croix en echec)
        if (n.busy) {
          uint16_t pf = (frame / 5) % 2 ? dim(TFT_CYAN) : TFT_CYAN;  // pulsation ~1 Hz
          if (n.sys == san::Sys::Wifi) iconWifi(16, cy, 10, pf); else iconServer(16, cy, 8, pf, true);
        } else if (n.type == san::NudgeType::Done) iconCheck(16, cy, 10, fg);
        else if (n.sys == san::Sys::Wifi) iconWifi(16, cy, 10, TFT_WHITE, true);
        else iconServer(16, cy, 8, TFT_WHITE, false);
      } else if (n.voice) iconSpeaker(16, cy, 10, fg, true);
      else iconNudge(ns.icon, 16, cy, 10, fg);
      textStyle(kFont24, TFT_WHITE, bg);
      d.setCursor(34, y);
      d.print(san::truncateChars(n.name, 11).c_str());
      if (n.count > 1) {
        textStyle(kFont20, TFT_WHITE, bg);
        d.setTextDatum(top_right);
        d.drawString(("x" + std::to_string(n.count)).c_str(), W - 4, y + 2);
        d.setTextDatum(top_left);
      }
    }
  } else {
    // aucune notification : le haut n'affiche que ce texte, discret ; la scene occupe la zone du dessous
    textStyle(kFont24, TFT_DARKGREY, TFT_BLACK);
    d.setTextDatum(middle_center);
    d.drawString("Pas de message", W / 2, lineH / 2);
    d.setTextDatum(top_left);
    drawScene(d, v.scene, frame);
  }
  d.drawFastHLine(0, 76, W, 0x39E7);
  // ---- zone d'activite (ligne A)
  const int ay = 91;
  bool any = v.pending > 0;
  iconBell(14, ay, 11, any ? TFT_ORANGE : TFT_DARKGREY);
  textStyle(kFont24, any ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  d.setCursor(30, ay - 12);
  d.print(san::formatCount(v.pending).c_str());
  if (v.activity.working > 0) iconGear(d, 98, ay, 12, TFT_CYAN, TFT_BLACK, frame * 0.12f);
  else iconGear(d, 98, ay, 12, TFT_DARKGREY, TFT_BLACK, 0);
  textStyle(kFont24, v.activity.working > 0 ? TFT_CYAN : TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(116, ay - 12);
  d.print((std::to_string(v.activity.working) + "/" + std::to_string(v.activity.followed)).c_str());
  if (v.activity.working > 0) {  // petite barre de progression glissante sous l'engrenage
    int bx = 84, bw = 68, x = bx + (int)((frame * 3) % (bw + 20)) - 20;
    d.drawFastHLine(bx, ay + 16, bw, 0x39E7);
    int x0 = x < bx ? bx : x, x1 = x + 20 > bx + bw ? bx + bw : x + 20;
    if (x1 > x0) d.fillRect(x0, ay + 15, x1 - x0, 3, TFT_CYAN);
  }
  // ---- zone de statut (ligne B)
  const int by = 121;
  iconWifi(14, by, 11, v.wifiOk ? TFT_WHITE : TFT_DARKGREY, !v.wifiOk);
  iconServer(46, by, 10, v.serverOk ? TFT_WHITE : TFT_DARKGREY, v.serverOk);
  if (v.speakerOn) iconSpeaker(78, by, 11, TFT_WHITE, true);
  else iconSpeakerMuted(78, by, 11, TFT_DARKGREY, TFT_RED);
  iconMoon(110, by, 11, v.autoSleep ? TFT_YELLOW : TFT_DARKGREY, TFT_BLACK);
  if (!v.autoSleep) wide(100, by + 9, 120, by - 9, 2.5f, TFT_RED);
  san::BatteryStyle bs = san::batteryStyle(v.battery, v.charge);
  iconBatteryEx(150, by - 8, 32, 16, bs);
  textStyle(kFont20, bs.low ? TFT_RED : TFT_WHITE, TFT_BLACK);
  d.setCursor(190, by - 10);
  d.print(san::formatBattery(v.battery).c_str());
}

static void presentMain(const MainView &v) {
  if (!gCvTried) {
    gCvTried = true;
    gCv.setPsram(true);
    gCv.setColorDepth(16);
    gCv.createSprite(M5.Display.width(), M5.Display.height());
  }
  uint32_t frame = gAnimFrame;
  if (gCv.getBuffer()) {  // dessin hors-ecran : pas de scintillement
    T = &gCv;
    drawMain(v, frame);
    T = &M5.Display;
    gCv.pushSprite(0, 0);
  } else {
    drawMain(v, frame);
  }
}

void mainView(const MainView &v) {
  gLast = v;
  gHaveLast = true;
  bool busy = false;
  for (auto &n : v.notifs) busy = busy || n.busy;
  gAnim = busy || v.activity.working > 0 || v.notifs.empty();  // scene : respiration lente meme au repos
  gAnimPeriod = (busy || v.activity.working > 0) ? 80 : 120;
  presentMain(v);
}

// Anime l'engrenage / la barre (~12 i/s) si l'ecran principal est affiche et qu'un agent travaille.
void mainTick() {
  if (!gAnim || !gHaveLast) return;
  uint32_t now = millis();
  if (now - gAnimLast < gAnimPeriod) return;
  gAnimLast = now;
  gAnimFrame++;
  presentMain(gLast);
}

void stopAnim() { gAnim = false; }

static void iconNudge(san::NudgeIcon ic, int cx, int cy, int r, uint16_t fg) {
  switch (ic) {
    case san::NudgeIcon::Play: iconPlay(cx, cy, r, fg); break;
    case san::NudgeIcon::Question: iconQuestion(cx, cy, r, fg); break;
    case san::NudgeIcon::Check: iconCheck(cx, cy, r, fg); break;
    case san::NudgeIcon::Cross: iconCross(cx, cy, r, fg); break;
    default: iconBell(cx, cy, r, fg); break;
  }
}

void recording(const std::string &agentName, int ms, int maxMs) {
  gAnim = false;
  auto &d = (*T);
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

void portal(const std::string &apName, int remainingPct) {
  gAnim = false;
  auto &d = (*T);
  d.fillScreen(TFT_BLACK);
  d.fillRect(0, 0, d.width(), 44, TFT_YELLOW);
  iconWifi(d.width() / 2, 22, 19, TFT_BLACK);
  textStyle(kFont24, TFT_WHITE, TFT_BLACK);
  d.setTextDatum(middle_center);
  d.drawString(apName.c_str(), d.width() / 2, 82);
  d.setTextDatum(top_left);
  if (remainingPct < 0) remainingPct = 0;
  if (remainingPct > 100) remainingPct = 100;
  d.drawRoundRect(10, 114, d.width() - 20, 10, 4, TFT_DARKGREY);
  if (remainingPct) d.fillRoundRect(12, 116, (d.width() - 24) * remainingPct / 100, 6, 2, TFT_YELLOW);
}

static void iconReset(int cx, int cy, int r, uint16_t fg) {  // fleche circulaire
  auto &d = *T;
  d.fillArc(cx, cy, r * 55 / 100, r * 85 / 100, 300, 240, fg);  // anneau ouvert en haut a gauche
  d.fillTriangle(cx - r * 100 / 100, cy - r * 15 / 100, cx - r * 30 / 100, cy - r * 15 / 100, cx - r * 65 / 100, cy + r * 35 / 100, fg);
}

void resetConfirm(int progressPct, int secondsLeft, bool holding) {
  gAnim = false;
  auto &d = (*T);
  uint16_t bg = dim(TFT_RED);
  d.fillScreen(bg);
  iconReset(34, 34, 26, TFT_WHITE);
  textStyle(kFont40, TFT_WHITE, bg);
  d.setTextDatum(middle_center);
  d.drawString(holding ? std::to_string(secondsLeft).c_str() : "3", 150, 34);
  d.setTextDatum(top_left);
  d.drawRoundRect(10, 76, d.width() - 20, 22, 6, TFT_WHITE);
  if (progressPct > 0) d.fillRoundRect(12, 78, (d.width() - 24) * progressPct / 100, 18, 4, TFT_RED);
  textStyle(kFont20, holding ? TFT_WHITE : TFT_LIGHTGREY, bg);
  d.setTextDatum(middle_center);
  d.drawString(holding ? "Maintenir..." : "Maintenir le haut 3 s", d.width() / 2, 112);
  d.setTextDatum(top_left);
}

void menuRoot(size_t cursor, const std::string &volume, const std::string &brightness, const std::string &pause) {
  gAnim = false;
  auto &d = M5.Display;
  const int W = d.width(), rowH = 27;
  const size_t kVisible = 5, total = san::kMenuRootRows;
  d.fillScreen(TFT_BLACK);
  const char *labels[] = {"Volume", "Lumiere", "Pause", "Config Wi-Fi (AP)", "Reinitialiser", "Sortir"};
  std::string values[] = {volume, brightness, pause, "", "", ""};
  size_t first = san::menuFirstRow(cursor, total, kVisible);
  for (size_t k = 0; k < kVisible && first + k < total; k++) {
    size_t i = first + k;
    int y = (int)k * rowH, cy = y + rowH / 2 - 1;
    bool cur = i == cursor;
    uint16_t bg = cur ? kSelBg : TFT_BLACK;
    d.fillRect(0, y, W - 5, rowH - 2, bg);
    if (cur) d.fillRect(0, y, 4, rowH - 2, TFT_PURPLE);
    switch (i) {
      case 0: if (volume == "Muet") iconSpeakerMuted(20, cy, 10, TFT_WHITE, TFT_RED); else iconSpeaker(20, cy, 10, TFT_WHITE, true); break;
      case 1: iconSun(20, cy, 10, TFT_YELLOW); break;
      case 2: iconMoon(20, cy, 10, TFT_YELLOW, bg); break;
      case 3: iconWifi(20, cy, 10, TFT_YELLOW); break;
      case 4: iconReset(20, cy, 10, TFT_RED); break;
      default: iconBack(20, cy, 10, TFT_WHITE); break;
    }
    textStyle(kFont20, TFT_WHITE, bg);
    d.setCursor(40, y + 4);
    d.print(labels[i]);
    d.setTextDatum(top_right);
    d.drawString(values[i].c_str(), W - 10, y + 4);
    d.setTextDatum(top_left);
  }
  if (total > kVisible) {  // ascenseur discret
    int th = 135 * (int)kVisible / (int)total, ty = (135 - th) * (int)first / (int)(total - kVisible);
    d.fillRect(W - 3, 0, 3, 135, 0x2104);
    d.fillRect(W - 3, ty, 3, th, TFT_LIGHTGREY);
  }
}

}  // namespace ui
