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

// Nom de l'agent affiche en grand (sans notification) : nom de la config (sinon le slug), tronque a
// `maxChars` caracteres avec "..." (sans couper un caractere UTF-8).
std::string agentLabel(const Config &cfg, const AgentState &s, int maxChars);

// ---- Rangee d'icones (aucune notification) : un pictogramme par agent suivi ----
constexpr size_t kSceneMax = 6;  // emplacements au maximum (le dernier devient "+N" au-dela)
struct SceneAgent {
  Icon icon;       // pictogramme d'etat (statusIcon)
  uint16_t color;  // couleur pleine RGB565 ; l'affichage l'attenue si !animated
  bool animated;   // working : anime ; sinon statique et attenue
  bool selected;   // agent cible (souligne ; son nom est affiche en grand)
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

// ---- Mise en page de la vue principale (paysage 240x135) ----
constexpr int kScreenW = 240, kScreenH = 135;
constexpr int kStatusY = 109, kStatusH = 26;  // ligne de statut en bas ; separateur en y = kStatusY - 1
constexpr int kMaxNotifLines = 3;
constexpr int kSummaryH = 24;  // ligne de resume (avec notifications)
constexpr int kNameH = 42;     // hauteur reservee au nom de l'agent (police 40)
// Avec n notifications (n > 0) : lignes empilees en haut (hauteur selon n), puis le resume juste dessous.
struct NotifLayout {
  int shown;      // lignes affichees (0..3)
  int lineH;      // hauteur d'une ligne
  int summaryY;   // haut de la ligne de resume (0 si aucune notification)
};
NotifLayout notifLayout(int n);
// Sans notification : rangee d'icones (`slots` emplacements, "+N" compris) et nom, centres dans y 0..kStatusY-2.
struct IconRow {
  int r;        // rayon des icones (<= 24)
  int slotW;    // largeur d'un emplacement
  int cy;       // ordonnee du centre des icones
  int nameCy;   // ordonnee du centre du nom
};
IconRow iconRowLayout(int slots, int width = kScreenW);
// Ligne de resume : total en attente (cloche) et, s'il y en a, agents en activite (engrenage anime).
struct Summary {
  std::string pending;
  bool showWorking;
  std::string working;
};
Summary buildSummary(int pending, int working);

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
enum class MenuItem { Volume, Brightness, Pause, WifiConfig, Reset, Exit };
constexpr size_t kMenuRootRows = 6;
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
  Close
};
struct MenuState {
  size_t cursor = 0;  // ligne courante
  MenuItem item() const { return (MenuItem)(cursor < kMenuRootRows ? cursor : kMenuRootRows - 1); }
};
// Frontal clic = defiler ; haut clic = modifier la ligne (valeur suivante / ouvrir / sortir) ;
// frontal long = sortir. Idle = tout fermer.
MenuAct menuStep(MenuState &m, MenuEvent e);

}  // namespace san
