// Ecrans (paysage 240x135). Toute la decision d'affichage est dans san_core ; ici, le dessin seulement.
#pragma once
#include <Arduino.h>

#include <string>
#include <vector>

#include "san_types.h"
#include "san_ui.h"

namespace ui {
void begin();
void backlightOn();
void backlightOff();
void message(const char *title, const std::string &text, uint16_t color = 0xFFFF);
void pairing(const std::string &code, const std::string &url);
// Ligne de notification (zone du haut).
struct NotifLine {
  std::string name;
  san::NudgeType type = san::NudgeType::Notification;
  bool voice = false, high = false;
  int count = 1;
};
// Tout ce que la vue principale affiche (aucune logique de decision ici).
struct MainView {
  std::vector<NotifLine> notifs;  // les plus recentes d'abord (max cfg::kNotifLines affichees)
  int pending = 0;                // total en attente
  san::Activity activity;
  bool hasSelected = false;       // agent cible (selectionne) : pastille + nom ; en grand s'il n'y a pas de notification
  std::string selName, selStatus;
  bool selStale = false;
  int selProgress = -1;
  bool wifiOk = false, serverOk = false, speakerOn = true, autoSleep = true;
  int battery = -1;
  san::Charge charge = san::Charge::Unknown;
};
void mainView(const MainView &v);
// Anime l'engrenage/la barre d'activite ; appel frequent, sans effet si rien ne travaille.
void mainTick();
void stopAnim();
void setBrightness(int b);  // 5-255, applique immediatement et memorise pour backlightOn
void recording(const std::string &agentName, int ms, int maxMs);
// Menu racine : curseur et valeurs courantes des reglages.
void menuRoot(size_t cursor, const std::string &volume, const std::string &brightness, const std::string &pause,
              const std::string &agents);
// Liste cochable des agents (derniere ligne : Enregistrer).
void menuAgents(const san::Config &cfg, size_t cursor, const std::string &note);
}  // namespace ui
