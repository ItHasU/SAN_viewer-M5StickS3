// File locale des notifications en attente (nudges/vocaux recus, pas encore acquittes). Logique pure.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "san_types.h"

namespace san {

struct Notif {
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
  // Nombre total en attente (somme des compteurs).
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

}  // namespace san
