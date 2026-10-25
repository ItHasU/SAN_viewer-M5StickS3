// Sons des nudges : synthese marimba (docs/SONS.md), type de nudge et regles anti-bruit.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "san_types.h"

namespace san {

// "start" | "notification" | "question" | "done" | "error" ; vide ou inconnu -> Notification.
NudgeType parseNudgeType(const std::string &s);
const char *nudgeTypeName(NudgeType t);

// ---- Synthese marimba (docs/SONS.md) : 16 kHz mono 16 bits, rendue par blocs ----
constexpr int kSoundRate = 16000;
constexpr float kSoundPeak = 0.35f;       // pic normalise (~ -9 dBFS) ; le volume utilisateur s'applique par-dessus
constexpr float kErrorBoost = 1.0f;       // gain propre a "error" (grave faible sur le petit HP) : a regler a l'oreille
struct SoundNote {
  float freq;   // Hz
  float start;  // s
  float dur;    // s (la note se coupe a dur, rampe de 5 ms avant)
};
struct SoundSpec {
  const SoundNote *notes;
  size_t count;
  float gain;        // gain de normalisation precalcule (pas de recherche de pic a l'execution)
  bool rounded;      // true : grave arrondi (erreur) au lieu du marimba
  size_t samples;    // duree totale a 16 kHz (derniere note + 0,3 s de queue)
};
const SoundSpec &nudgeSound(NudgeType t);
inline uint32_t nudgeSoundMs(NudgeType t) { return (uint32_t)(nudgeSound(t).samples * 1000u / kSoundRate); }
// Synthetise `n` echantillons du son a partir de l'echantillon `offset` ; au-dela de la fin : silence.
void soundRender(NudgeType t, size_t offset, int16_t *out, size_t n);

// ---- Regles anti-bruit (SONS.md section 6), horloge fournie par l'appelant (ms) ----
constexpr uint32_t kSoundGapMs = 2000;  // delai minimal entre la fin d'un son et le debut du suivant
constexpr bool kStartMuted = true;      // "start" muet par defaut
// Priorite : error > question > done > notification > start.
int soundPriority(NudgeType t);
class SoundScheduler {
 public:
  // Demande un son. Les demandes en attente fusionnent : seul le type le plus prioritaire joue.
  void request(NudgeType t);
  // A appeler regulierement. `busy` = un son est en cours de lecture. Renvoie true et fixe `out` quand il faut
  // lancer un son maintenant (un seul a la fois, 2 s apres la fin du precedent).
  bool poll(uint32_t nowMs, bool busy, NudgeType &out);
  bool pending() const { return has_; }
  void clear() { has_ = false; }

 private:
  bool has_ = false, playing_ = false, haveEnd_ = false;
  NudgeType type_ = NudgeType::Notification;
  uint32_t endAt_ = 0;
};

}  // namespace san
