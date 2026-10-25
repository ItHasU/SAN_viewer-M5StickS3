// File locale des notifications en attente (nudges/vocaux recus, pas encore acquittes). Logique pure.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "san_types.h"

namespace san {

// Notifications systeme locales (etat de connexion) : pseudo-agents, jamais envoyes au serveur ni confondus avec un agent.
enum class Sys : uint8_t { None = 0, Wifi, Server };
enum class SysState : uint8_t { Connecting, Failed, Recovered };
const char *sysName(Sys s);  // nom court affiche : "Wi-Fi", "Serveur" ("" pour None)
constexpr uint32_t kRecoveredMs = 4000;  // duree d'affichage de "... OK" avant auto-acquittement

struct Notif {
  Sys sys = Sys::None;  // != None : entree systeme (agent vide)
  bool busy = false;    // connexion en cours (entree transitoire, non comptee, animee)
  uint32_t expiresAt = 0;  // ms (millis) d'auto-acquittement ; 0 = jamais
  std::string agent;  // slug
  NudgeType type = NudgeType::Notification;
  bool voice = false;  // message vocal (icone haut-parleur)
  bool high = false;
  int count = 1;       // doublons fusionnes (meme agent + meme type)
  int64_t id = 0;      // id du dernier evenement fusionne : croissant = plus recent
};

class NotifQueue {
 public:
  static constexpr size_t kCapacity = 16;
  // Ajoute une notification ; fusionne avec une entree existante de meme agent/type/voix (compteur++, id mis a jour,
  // priorite haute conservee). File pleine : l'entree la plus ancienne est supprimee.
  void push(const std::string &agent, NudgeType type, bool voice, bool high, int64_t id);
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  // Entree systeme : REMPLACE l'entree du meme Sys quand l'etat change (compteur 1, redevient la plus recente) ;
  // meme etat repete : fusion (compteur++). Connecting = busy ; Failed = Error ; Recovered = Done qui expire
  // apres kRecoveredMs.
  void setSystem(Sys s, SysState st, uint32_t nowMs);
  bool clearSystem(Sys s);
  // Retire les entrees dont l'echeance est passee. Renvoie le nombre retire.
  size_t expire(uint32_t nowMs);
  bool hasBusy() const;
  // Nombre total en attente (somme des compteurs ; hors connexions en cours).
  int total() const;
  // index 0 = la plus recente. Hors limites : nullptr.
  const Notif *recent(size_t index) const;
  // Les n plus recentes, la plus recente d'abord.
  std::vector<Notif> latest(size_t n) const;
  // Acquitte (retire) l'entree la plus recente / une entree par rang / tout / tout un agent. Renvoie true si retire.
  bool ackLatest();
  bool ackRecent(size_t index);
  void ackAll() { items_.clear(); }
  size_t ackAgent(const std::string &agent);
  // Acces brut (ordre chronologique, ancien -> recent) pour la persistance.
  const std::vector<Notif> &items() const { return items_; }
  void restore(const std::vector<Notif> &v);  // tronque a kCapacity, garde l'ordre

 private:
  std::vector<Notif> items_;  // ancien -> recent
};

// Son a jouer pour un lot de nudges : le plus important (erreur > question > notification > fin > debut ;
// priorite haute d'abord). Renvoie false si la liste est vide.
bool pickAlert(const std::vector<EventItem> &events, NudgeType &type, bool &high);

// Suivi des echecs consecutifs d'un lien (Wi-Fi ou serveur) et decision d'affichage / de son.
struct ConnOutcome {
  bool show = false;  // false : succes sans echec precedent -> aucune entree
  SysState state = SysState::Failed;  // Failed ou Recovered
  bool sound = false;                 // son d'erreur (echec seulement)
};
// `fails` : compteur persistant, mis a jour. Son : ecran deja allume par l'utilisateur, ou echec persistant
// (2e echec consecutif exactement : pas de sonnerie a chaque reveil hors couverture).
ConnOutcome connReport(uint8_t &fails, bool ok, bool screenOn);

}  // namespace san
