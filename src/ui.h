// Ecrans (paysage 240x135). Toute la decision d'affichage est dans san_core ; ici, le dessin seulement.
#pragma once
#include <Arduino.h>

#include <string>
#include <vector>

#include "san_notify.h"
#include "san_types.h"
#include "san_ui.h"

namespace ui {
void begin();
void backlightOn();
void backlightOff();
void message(const char *title, const std::string &text, uint16_t color = 0xFFFF);
void pairing(const std::string &code);  // code d'appairage seul, en grand
// Portail Wi-Fi (point d'acces) : icone Wi-Fi + nom du reseau ; barre = temps restant avant abandon (0-100).
void portal(const std::string &apName, int remainingPct);
// Confirmation de reinitialisation : barre de maintien (0-100) et compte a rebours ; holding = bouton enfonce.
void resetConfirm(int progressPct, int secondsLeft, bool holding);
// Ligne de notification (zone du haut).
struct NotifLine {
  std::string name;
  san::NudgeType type = san::NudgeType::Notification;
  bool voice = false, high = false;
  int count = 1;
  san::Sys sys = san::Sys::None;  // etat de connexion local (pas un agent)
  bool busy = false;              // connexion en cours : icone qui pulse
};
// Tout ce que la vue principale affiche (aucune logique de decision ici).
struct MainView {
  std::vector<NotifLine> notifs;  // les plus recentes d'abord (max cfg::kNotifLines affichees)
  int pending = 0;                // total en attente
  san::Activity activity;
  san::Scene scene;               // sans notification : un pictogramme par agent suivi (aucun nom)
  bool wifiOk = false, serverOk = false, speakerOn = true, autoSleep = true;
  int battery = -1;
  san::Charge charge = san::Charge::Unknown;
};
void mainView(const MainView &v);
// Anime la scene / l'engrenage / la barre d'activite ; appel frequent, sans effet si rien a animer.
void mainTick();
void stopAnim();
void setBrightness(int b);  // 5-255, applique immediatement et memorise pour backlightOn
void recording(const std::string &agentName, int ms, int maxMs);
// Menu racine : curseur et valeurs courantes des reglages.
void menuRoot(size_t cursor, const std::string &volume, const std::string &brightness, const std::string &pause);
}  // namespace ui
