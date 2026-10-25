// Logique pure de l'interface : reglages (valeurs par pas), menu, actions des boutons, compteurs, batterie.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "san_logic.h"
#include "san_types.h"

namespace san {

// ---- Reglages : series de valeurs ----
constexpr int kDefaultVolume = 230;
constexpr int kDefaultBrightness = 110;
constexpr int kDefaultPauseS = 30;  // 0 = jamais (veille automatique desactivee)

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

// ---- Compteurs ----
std::string formatCount(int n);  // "0".."99", "99+"
struct Activity {
  int working = 0;   // agents "working" non perimes parmi les agents suivis
  int followed = 0;  // agents suivis
};
// `states` = etats deja filtres/ordonnes (orderStates).
Activity summarizeActivity(const std::vector<AgentState> &states);
// Agent suivant dans la liste (cyclique) ; index invalide ramene a 0 ; 0 si liste vide.
size_t nextAgentIndex(size_t cur, size_t count);
size_t clampAgentIndex(size_t cur, size_t count);

// ---- Scene principale (aucune notification) : un pictogramme par agent suivi ----
constexpr size_t kSceneMax = 6;  // emplacements au maximum (le dernier devient "+N" au-dela)
struct SceneAgent {
  Icon icon;       // pictogramme d'etat (statusIcon)
  uint16_t color;  // couleur pleine RGB565 ; l'affichage l'attenue si !animated
  bool animated;   // working : anime ; sinon statique et attenue
  bool selected;   // agent cible (souligne discretement, sans nom)
};
struct Scene {
  std::vector<SceneAgent> agents;  // emplacements affiches, dans l'ordre
  int more = 0;                    // agents non affiches ("+N" si > 0)
  bool calm = true;                // personne ne travaille : respiration lente
};
// `states` = etats suivis ordonnes (orderStates). Plus de maxShown agents : maxShown-1 emplacements + "+N" ;
// la fenetre glisse pour que l'agent selectionne reste visible.
Scene buildScene(const std::vector<AgentState> &states, size_t sel, size_t maxShown = kSceneMax);
// Decalage vertical (-2..2 px) de la respiration lente, periode 48 images.
int sceneBreath(uint32_t frame);

// ---- Batterie ----
enum class Charge { Unknown, Discharging, Charging };
struct BatteryStyle {
  int level;        // -1 inconnu, sinon 0..100
  uint16_t color;   // RGB565
  bool bolt;        // eclair (charge en cours)
  bool low;         // niveau critique (a faire clignoter / mettre en evidence)
};
BatteryStyle batteryStyle(int level, Charge charge);
std::string formatBattery(int level);  // "87%" ou "--"

// ---- Actions des boutons (vue principale) ----
// Haut = BtnB (acquitter / agent suivant), Frontal = BtnA (repeter / enregistrer / menu).
enum class Button { Top, Front };
enum class Press { Click, DoubleClick, Hold };
enum class MainAction { None, AckLatest, NextAgent, Replay, Record, OpenMenu };
MainAction mainAction(Button b, Press p, bool hasNotifications);

// ---- Machine a etats du menu ----
enum class MenuScreen { Root, Agents };
enum class MenuItem { Volume, Brightness, Pause, Agents, WifiConfig, Reset, Exit };
constexpr size_t kMenuRootRows = 7;
// Premiere ligne visible d'une liste de `total` lignes dont `visible` tiennent a l'ecran (le curseur reste visible).
size_t menuFirstRow(size_t cursor, size_t total, size_t visible);
// Reinitialisation : appui maintenu kResetHoldMs sur le bouton du dessus (compte a rebours), annulable.
constexpr uint32_t kResetHoldMs = 3000;
int resetProgress(uint32_t heldMs);       // 0..100
int resetSecondsLeft(uint32_t heldMs);    // 3, 2, 1, 0
inline bool resetConfirmed(uint32_t heldMs) { return heldMs >= kResetHoldMs; }
enum class MenuEvent { FrontClick, FrontHold, TopClick, TopHold, Idle };
enum class MenuAct {
  None, Redraw, VolumeNext, BrightnessNext, PauseNext,
  WifiConfig,    // portail Wi-Fi (point d'acces) pour la configuration
  AskReset,      // demande de reinitialisation : l'appelant ouvre l'ecran de confirmation (appui maintenu)
  OpenAgents,    // l'appelant charge la config puis appelle menuEnterAgents
  ToggleAgent, ToggleMute, SaveAgents, Close
};
struct MenuState {
  MenuScreen screen = MenuScreen::Root;
  size_t cursor = 0;       // ligne courante de l'ecran courant
  size_t rootCursor = 0;   // curseur memorise du menu racine
  size_t agentCount = 0;   // lignes agents ; la derniere ligne (index agentCount) = "Enregistrer"
  size_t rows() const { return screen == MenuScreen::Root ? kMenuRootRows : agentCount + 1; }
  MenuItem item() const { return (MenuItem)(cursor < kMenuRootRows ? cursor : kMenuRootRows - 1); }
  bool onSaveRow() const { return screen == MenuScreen::Agents && cursor == agentCount; }
};
// Racine : frontal clic = defiler ; haut clic = modifier la ligne (valeur suivante / entrer / sortir) ;
// frontal long = sortir. Agents : frontal clic = defiler ; haut clic = cocher (ou enregistrer sur la derniere ligne) ;
// haut long = muet on/off ; frontal long = retour au menu racine sans enregistrer. Idle = tout fermer.
MenuAct menuStep(MenuState &m, MenuEvent e);
void menuEnterAgents(MenuState &m, size_t agentCount);
void menuBackToRoot(MenuState &m);

}  // namespace san
