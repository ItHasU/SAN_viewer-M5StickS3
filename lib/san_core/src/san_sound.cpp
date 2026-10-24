#include "san_sound.h"

namespace san {

NudgeType parseNudgeType(const std::string &s) {
  if (s == "start") return NudgeType::Start;
  if (s == "question") return NudgeType::Question;
  if (s == "done") return NudgeType::Done;
  if (s == "error") return NudgeType::Error;
  return NudgeType::Notification;
}

const char *nudgeTypeName(NudgeType t) {
  switch (t) {
    case NudgeType::Start: return "start";
    case NudgeType::Question: return "question";
    case NudgeType::Done: return "done";
    case NudgeType::Error: return "error";
    default: return "notification";
  }
}

namespace {
// start : deux notes montantes courtes.
const Note kStart[] = {{660, 90}, {0, 20}, {880, 140}};
// notification : "tu-di-tu-di-tu-di" (deux notes alternees, 3 fois).
const Note kNotification[] = {{880, 80}, {0, 20}, {1175, 80}, {0, 20}, {880, 80}, {0, 20},
                              {1175, 80}, {0, 20}, {880, 80}, {0, 20}, {1175, 80}};
// question : "ta-DA ?" deux notes montantes, la seconde tenue plus haut (en suspens).
const Note kQuestion[] = {{587, 140}, {0, 30}, {880, 120}, {0, 20}, {1047, 380}};
// done : arpege majeur do-mi-sol-do.
const Note kDone[] = {{523, 110}, {659, 110}, {784, 110}, {1047, 380}};
// error : buzzer grave descendant, repete 2 fois.
const Note kError[] = {{196, 130}, {147, 130}, {110, 220}, {0, 80},
                       {196, 130}, {147, 130}, {110, 220}};

template <size_t N>
SoundPattern mk(const Note (&a)[N]) { return SoundPattern{a, N}; }

constexpr uint16_t kBlue = 0x249F, kOrange = 0xFD20, kPurple = 0x901F, kGreen = 0x07E0, kRed = 0xF800,
                   kYellow = 0xFFE0;
}  // namespace

SoundPattern nudgeSound(NudgeType t) {
  switch (t) {
    case NudgeType::Start: return mk(kStart);
    case NudgeType::Question: return mk(kQuestion);
    case NudgeType::Done: return mk(kDone);
    case NudgeType::Error: return mk(kError);
    default: return mk(kNotification);
  }
}

int nudgeRepeats(bool high) { return high ? 2 : 1; }

uint32_t nudgeSoundMs(NudgeType t, bool high) {
  SoundPattern p = nudgeSound(t);
  uint32_t one = 0;
  for (size_t i = 0; i < p.count; i++) one += p.notes[i].ms;
  // 150 ms de pause entre deux passages
  int r = nudgeRepeats(high);
  return one * r + 150u * (r - 1);
}

NudgeStyle nudgeStyle(NudgeType t) {
  switch (t) {
    case NudgeType::Start: return {NudgeIcon::Play, kBlue, kBlue};
    case NudgeType::Question: return {NudgeIcon::Question, kPurple, kYellow};
    case NudgeType::Done: return {NudgeIcon::Check, kGreen, kGreen};
    case NudgeType::Error: return {NudgeIcon::Cross, kRed, kRed};
    default: return {NudgeIcon::Bell, kOrange, kOrange};
  }
}

}  // namespace san
