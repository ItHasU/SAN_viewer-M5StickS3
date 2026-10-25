#include "san_sound.h"

#include <cmath>

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
// Notes en Hz : do5 523,25 ; re5 587,33 ; mi5 659,25 ; sol5 783,99 ; la5 880,00 ; re3 146,83 ; la3 220,00.
const SoundNote kStart[] = {{523.25f, 0.00f, 0.50f}, {659.25f, 0.16f, 0.60f}};
const SoundNote kNotification[] = {{783.99f, 0.00f, 0.90f}};
const SoundNote kQuestion[] = {{587.33f, 0.00f, 0.45f}, {880.00f, 0.20f, 0.90f}};
const SoundNote kDone[] = {{783.99f, 0.00f, 0.50f}, {659.25f, 0.20f, 0.50f}, {523.25f, 0.40f, 1.00f}};
const SoundNote kError[] = {{220.00f, 0.00f, 0.55f}, {146.83f, 0.28f, 0.80f}};

// Gains de normalisation (pic 0,35) calcules a 16 kHz avec les formules de SONS.md section 2 (gen_sons.py).
// Test natif : pic reel dans 0,35 +/- 2 %.
const SoundSpec kSpecs[] = {
    {kStart, 2, 0.213083f, false, 16960},
    {kNotification, 1, 0.279286f, false, 19200},
    {kQuestion, 2, 0.223363f, false, 22400},
    {kDone, 3, 0.208341f, false, 27200},
    {kError, 2, 0.182240f * kErrorBoost, true, 22080},
};

constexpr float kTwoPi = 6.28318530718f;

float noteSample(const SoundNote &n, float t, bool rounded) {
  float s;
  float w = kTwoPi * n.freq * t;
  if (rounded) {
    s = sinf(w) + 0.6f * sinf(2 * w) + 0.3f * sinf(3 * w);
    s *= expf(-t / 0.28f) * (t < 0.012f ? t / 0.012f : 1.0f);
  } else {
    s = sinf(w) * expf(-t / 0.22f) + 0.35f * sinf(4 * w) * expf(-t / 0.05f);
    if (t < 0.002f) s *= t / 0.002f;
  }
  float left = n.dur - t;  // rampe de 5 ms avant la coupure
  if (left < 0.005f) s *= left > 0 ? left / 0.005f : 0.0f;
  return s;
}
}  // namespace

const SoundSpec &nudgeSound(NudgeType t) {
  switch (t) {
    case NudgeType::Start: return kSpecs[0];
    case NudgeType::Question: return kSpecs[2];
    case NudgeType::Done: return kSpecs[3];
    case NudgeType::Error: return kSpecs[4];
    default: return kSpecs[1];
  }
}

void soundRender(NudgeType type, size_t offset, int16_t *out, size_t n) {
  const SoundSpec &sp = nudgeSound(type);
  for (size_t k = 0; k < n; k++) {
    size_t idx = offset + k;
    if (idx >= sp.samples) { out[k] = 0; continue; }
    float t = (float)idx / (float)kSoundRate, v = 0;
    for (size_t i = 0; i < sp.count; i++) {
      const SoundNote &nt = sp.notes[i];
      if (t >= nt.start && t < nt.start + nt.dur) v += noteSample(nt, t - nt.start, sp.rounded);
    }
    v *= sp.gain * 32767.0f;
    out[k] = (int16_t)(v > 32767.0f ? 32767 : v < -32767.0f ? -32767 : lroundf(v));
  }
}

int soundPriority(NudgeType t) {
  switch (t) {
    case NudgeType::Error: return 5;
    case NudgeType::Question: return 4;
    case NudgeType::Done: return 3;
    case NudgeType::Notification: return 2;
    default: return 1;
  }
}

void SoundScheduler::request(NudgeType t) {
  if (kStartMuted && t == NudgeType::Start) return;
  if (!has_ || soundPriority(t) > soundPriority(type_)) type_ = t;
  has_ = true;
}

bool SoundScheduler::poll(uint32_t nowMs, bool busy, NudgeType &out) {
  if (playing_ && !busy) { playing_ = false; haveEnd_ = true; endAt_ = nowMs; }
  if (!has_ || busy || playing_) return false;
  if (haveEnd_ && (uint32_t)(nowMs - endAt_) < kSoundGapMs) return false;
  out = type_;
  has_ = false;
  playing_ = true;
  return true;
}

}  // namespace san
