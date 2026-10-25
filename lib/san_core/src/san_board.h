// Etat d'affichage des agents (icone du dernier statut, pastille de vocaux), auto-selection et detection des
// changements entre deux synchronisations. Logique pure (sans HAL).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "san_types.h"

namespace san {

// Icone d'un agent d'apres le dernier statut serveur (champ `status` de states[]).
//   idle | done -> Ready (coche verte) ; error -> Problem (!) ; waiting -> Question (?) ;
//   working -> Thinking (animation) ; perime ou inconnu -> Unknown (tiret gris).
enum class Mood : uint8_t { Unknown = 0, Thinking, Ready, Problem, Question };
Mood moodFromStatus(const std::string &status, bool stale);
uint16_t moodColor(Mood m);  // RGB565

// Priorite d'auto-selection : vocal 4 > question 3 > probleme 2 > pret 1. "Reflexion" et inconnu : 0 (pas de changement).
constexpr int kRankVoice = 4;
int moodRank(Mood m);

// Etat precedent d'un agent (cache RTC) pour detecter les changements.
struct PrevAgent {
  std::string agent;
  std::string status;           // vide = inconnu (premiere apparition) : aucun changement signale
  std::vector<uint16_t> seen;   // empreintes des clips deja vus (voir clipHash)
};
struct VoiceArrival {
  std::string agent;
  std::vector<std::string> clips;  // clips apparus depuis la derniere synchronisation, du plus ancien au plus recent
};
struct SyncDiff {
  bool changed = false;  // au moins un changement de statut notable ou un vocal : allumer l'ecran
  std::string pick;      // agent a selectionner ("" = aucun)
  int pickRank = 0;
  std::vector<VoiceArrival> arrivals;
};
// Compare les etats recus au cache. Ordre de priorite pour la selection : vocal > question > probleme > pret ;
// a rang egal, le dernier agent de la liste. Les agents perimes ou inconnus du cache ne declenchent rien.
SyncDiff diffStates(const std::vector<PrevAgent> &prev, const std::vector<AgentState> &states);

// Empreinte 16 bits (FNV-1a) d'un identifiant de clip, et memoire bornee (kSeenMax) des clips vus.
constexpr size_t kSeenMax = 8;
uint16_t clipHash(const std::string &clipId);
void updateSeen(std::vector<uint16_t> &seen, const std::vector<std::string> &clips);

// ---- Rangee d'agents de l'ecran principal ----
constexpr size_t kAgentSlots = 6;  // emplacements au maximum ("+N" au-dela)
struct AgentCell {
  Mood mood;
  int voices;     // vocaux en attente (pastille en bas a droite si > 0)
  bool selected;
};
struct AgentRow {
  std::vector<AgentCell> cells;
  int more = 0;           // agents non affiches ("+N")
  bool thinking = false;  // au moins un agent reflechit (animation)
};
// `states` = etats ordonnes (orderStates). Plus de maxShown agents : maxShown-1 emplacements + "+N" ; la fenetre
// glisse pour garder l'agent selectionne visible.
AgentRow buildAgentRow(const std::vector<AgentState> &states, size_t sel, size_t maxShown = kAgentSlots);
std::string formatBadge(int n);  // "1".."9", "9+"

// Mise en page (paysage 240x135) : zone d'agents en haut, nom sous les icones, barre de statut en bas.
constexpr int kScreenW = 240, kScreenH = 135;
constexpr int kStatusY = 109, kStatusH = 26;  // barre de statut ; separateur en y = kStatusY - 1
constexpr int kAgentsH = 74;                  // zone d'icones : y 0..kAgentsH
constexpr int kNameCy = 92;                   // centre de la ligne du nom (police 24 : ~ y 79..105)
struct AgentRowLayout {
  int r;       // rayon des icones (<= 24)
  int slotW;   // largeur d'un emplacement
  int cy;      // ordonnee du centre des icones
};
AgentRowLayout agentRowLayout(int slots, int width = kScreenW);
// Decalage vertical (-2..2 px) de la respiration lente, periode 48 images.
int calmBreath(uint32_t frame);

// ---- Echecs de connexion : son d'erreur ----
// `fails` : compteur persistant, mis a jour. Son : ecran deja allume par l'utilisateur, ou echec persistant
// (2e echec consecutif exactement : pas de sonnerie a chaque reveil hors couverture).
bool connReportSound(uint8_t &fails, bool ok, bool screenOn);

}  // namespace san
