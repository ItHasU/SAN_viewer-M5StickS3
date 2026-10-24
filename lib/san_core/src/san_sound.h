// Sons et styles des nudges par type (donnees pures, jouees par le firmware via M5.Speaker.tone).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "san_types.h"

namespace san {

// "start" | "notification" | "question" | "done" | "error" ; vide ou inconnu -> Notification.
NudgeType parseNudgeType(const std::string &s);
const char *nudgeTypeName(NudgeType t);

struct Note {
  uint16_t freq;  // Hz, 0 = silence
  uint16_t ms;
};
struct SoundPattern {
  const Note *notes;
  size_t count;
};
// Motif d'un passage (le motif "notification" contient deja ses 3 tu-di).
SoundPattern nudgeSound(NudgeType t);
// Nombre de passages : 1 en priorite normale, 2 en priorite haute.
int nudgeRepeats(bool high);
// Duree totale du son pour un type et une priorite.
uint32_t nudgeSoundMs(NudgeType t, bool high);

enum class NudgeIcon { Play, Bell, Question, Check, Cross };
struct NudgeStyle {
  NudgeIcon icon;
  uint16_t color;   // RGB565 : teinte du type (fond vif en priorite haute, fond sombre sinon)
  uint16_t accent;  // couleur du pictogramme sur fond sombre
};
NudgeStyle nudgeStyle(NudgeType t);

}  // namespace san
