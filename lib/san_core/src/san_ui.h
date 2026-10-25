// Logique pure de l'interface : reglages (valeurs par pas), menu, actions des boutons, batterie.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "san_logic.h"
#include "san_types.h"

namespace san {

// ---- Reglages : series de valeurs ----
constexpr int kDefaultVolume = 230;       // volume des notifications (sons de nudge) et des vocaux (voix) : 0-255
constexpr int kDefaultBrightness = 110;
constexpr int kDefaultPauseS = 30;        // 0 = jamais (veille automatique desactivee)
constexpr bool kDefaultAutoPlay = true;   // lecture automatique des vocaux

// Cycle sur la valeur de la serie la plus proche (la valeur courante peut venir d'une ancienne version).
int nextVolume(int cur);
int nextBrightness(int cur);
int nextPause(int cur);
// Valeurs lues en NVS : bornees / ramenees a une valeur valide.
int sanitizeVolume(int v);
int sanitizeBrightness(int v);
int sanitizePause(int v);
std::string formatVolume(int v);      // "Muet" ou "90%"
std::string formatBrightness(int v);  // "43%"
std::string formatPause(int s);       // "10 s", "1 min", "Jamais"
std::string formatYesNo(bool v);      // "Oui" / "Non"

// Agent suivant dans la liste (cyclique) ; index invalide ramene a 0 ; 0 si liste vide.
size_t nextAgentIndex(size_t cur, size_t count);
size_t clampAgentIndex(size_t cur, size_t count);
// Index de l'agent `slug` dans la liste ; `fallback` (borne) s'il est absent.
size_t agentIndexBySlug(const std::vector<AgentState> &states, const std::string &slug, size_t fallback);

// Nom de l'agent affiche (ligne de texte moyenne) : nom de la config (sinon le slug), tronque a
// `maxChars` caracteres avec "..." (sans couper un caractere UTF-8).
std::string agentLabel(const Config &cfg, const AgentState &s, int maxChars);

// ---- Batterie ----
enum class Charge { Unknown, Discharging, Charging };
struct BatteryStyle {
  int level;        // -1 inconnu, sinon 0..100
  uint16_t color;   // RGB565
  bool bolt;        // eclair (charge en cours)
  bool low;         // niveau critique (a mettre en evidence)
};
BatteryStyle batteryStyle(int level, Charge charge);
std::string formatBattery(int level);  // "87%" ou "--"

// ---- Boutons : ecran principal ----
// Dessus : clic = agent suivant, appui long = ouvrir les parametres.
// Frontal : clic = lire le vocal de l'agent selectionne, appui long = enregistrer.
enum class Button { Top, Front };
enum class Press { Click, Hold };
enum class MainAction { None, NextAgent, OpenMenu, PlayVoice, Record };
MainAction mainAction(Button b, Press p);

// ---- Menu des parametres ----
enum class MenuItem { VolNotif, VolVoice, Brightness, Pause, AutoPlay, WifiConfig, Reset };
constexpr size_t kMenuRows = 7;
const char *menuLabel(MenuItem it);
// Premiere ligne visible d'une liste de `total` lignes dont `visible` tiennent a l'ecran (le curseur reste visible).
size_t menuFirstRow(size_t cursor, size_t total, size_t visible);
// Reinitialisation : bouton frontal maintenu kResetHoldMs (compte a rebours) ; bouton du dessus = annuler.
constexpr uint32_t kResetHoldMs = 3000;
int resetProgress(uint32_t heldMs);       // 0..100
int resetSecondsLeft(uint32_t heldMs);    // 3, 2, 1, 0
inline bool resetConfirmed(uint32_t heldMs) { return heldMs >= kResetHoldMs; }
enum class MenuEvent { TopClick, TopHold, FrontClick, FrontHold, Idle };
enum class MenuAct {
  None, Redraw, VolNotifNext, VolVoiceNext, BrightnessNext, PauseNext, AutoPlayToggle,
  WifiConfig,    // portail Wi-Fi (point d'acces) pour la configuration
  AskReset,      // demande de reinitialisation : l'appelant ouvre l'ecran de confirmation (appui maintenu)
  Close
};
struct MenuState {
  size_t cursor = 0;  // ligne courante
  MenuItem item() const { return (MenuItem)(cursor < kMenuRows ? cursor : kMenuRows - 1); }
};
// Dessus clic = element suivant ; frontal clic = agir sur l'element ; dessus long = quitter ; frontal long :
// rien. Idle = tout fermer.
MenuAct menuStep(MenuState &m, MenuEvent e);

}  // namespace san
