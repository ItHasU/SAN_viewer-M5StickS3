// Ecrans (paysage 240x135). Toute la decision d'affichage est dans san_core ; ici, le dessin seulement.
#pragma once
#include <Arduino.h>

#include <string>
#include <vector>

#include "san_board.h"
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
// Tout ce que l'ecran principal affiche (aucune logique de decision ici).
struct MainView {
  san::AgentRow row;              // icones des agents (dernier statut), pastilles de vocaux, selection
  bool hasSel = false;
  std::string selName;            // nom de l'agent selectionne (ligne de texte moyenne)
  bool wifiOk = false, serverOk = false, speakerOn = true, autoSleep = true;  // barre de statut
  int battery = -1;
  san::Charge charge = san::Charge::Unknown;
};
void mainView(const MainView &v);
// Anime l'ecran principal ; appel frequent, sans effet si rien a animer.
void mainTick();
void stopAnim();
void setBrightness(int b);  // 5-255, applique immediatement et memorise pour backlightOn
void recording(const std::string &agentName, int ms, int maxMs);
// Menu des parametres : une ligne par reglage (valeur en texte) ; `muted` = volume a zero (icone barree).
struct MenuRowView {
  san::MenuItem item;
  std::string label, value;
  bool muted = false;
};
void menu(size_t cursor, const std::vector<MenuRowView> &rows);
// Lecture d'un vocal : progression en ms.
void playback(const std::string &agentName, int ms, int totalMs);
}  // namespace ui
