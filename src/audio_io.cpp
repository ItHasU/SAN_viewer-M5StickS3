#include "audio_io.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "san_audio.h"
#include "san_sound.h"

namespace audio_io {

static int sVolume = 230;
static bool sSpkOn = false;

static bool speakerBegin() {
  M5.Mic.end();
  if (!sSpkOn) sSpkOn = M5.Speaker.begin();
  if (sSpkOn) M5.Speaker.setVolume(sVolume);
  return sSpkOn;
}

void setVolume(int v) {
  sVolume = v < 0 ? 0 : v > 255 ? 255 : v;
  if (sSpkOn) M5.Speaker.setVolume(sVolume);
}

void beep() {
  if (sVolume <= 0 || !speakerBegin()) return;
  M5.Speaker.tone(1000, 90);
}

static void waitPlayback(bool abortOnButton) {
  while (M5.Speaker.isPlaying()) {
    M5.update();
    if (abortOnButton && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
      M5.Speaker.stop();
      break;
    }
    delay(10);
  }
}

bool playWav(const uint8_t *data, size_t len, bool abortOnButton) {
  san::WavInfo w = san::parseWav(data, len);
  if (!w.valid || w.channels != 1) return false;
  const int16_t *pcm = nullptr;
  size_t n = 0;
  int16_t *dec = nullptr;
  if (w.format == 1 && w.bits == 16) {
    pcm = (const int16_t *)(data + w.dataOffset);  // alignement : l'offset est pair (en-tetes RIFF)
    n = w.dataLen / 2;
  } else if (w.format == 0x11) {
    size_t cnt = san::imaSampleCount(w);
    dec = (int16_t *)heap_caps_malloc(cnt * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dec) return false;
    n = san::imaDecode(w, data, dec, cnt);
    pcm = dec;
  } else {
    return false;
  }
  if (sVolume <= 0 || !speakerBegin()) {  // muet : rien a jouer
    if (dec) heap_caps_free(dec);
    return true;
  }
  // playRaw copie par references : le buffer doit rester valide jusqu'a la fin de lecture.
  M5.Speaker.playRaw(pcm, n, w.sampleRate, false, 1, 0);
  waitPlayback(abortOnButton);
  M5.Speaker.stop();
  if (dec) heap_caps_free(dec);
  return true;
}

// Sequenceur non bloquant : chaque note est lancee par soundTick() quand la precedente est finie.
static const san::Note *sNotes = nullptr;
static size_t sCount = 0, sIdx = 0;
static int sRepeatsLeft = 0;
static uint32_t sNextAt = 0;
static bool sActive = false;

void soundStart(san::NudgeType type, bool high) {
  if (sVolume <= 0 || !speakerBegin()) return;
  san::SoundPattern p = san::nudgeSound(type);
  sNotes = p.notes; sCount = p.count; sIdx = 0;
  sRepeatsLeft = san::nudgeRepeats(high);
  sNextAt = millis();
  sActive = true;
  soundTick();
}

void soundTick() {
  if (!sActive) return;
  while (sActive && (int32_t)(millis() - sNextAt) >= 0) {
    if (sIdx >= sCount) {
      if (--sRepeatsLeft <= 0) { sActive = false; break; }
      sIdx = 0;
      sNextAt += 150;  // pause entre deux passages
      continue;
    }
    const san::Note &n = sNotes[sIdx++];
    if (n.freq) M5.Speaker.tone(n.freq, n.ms);
    sNextAt += n.ms;
  }
}

bool soundBusy() { return sActive || M5.Speaker.isPlaying(); }

void soundWait() {
  while (soundBusy()) { soundTick(); delay(5); }
}

void soundStop() {
  sActive = false;
  M5.Speaker.stop();
}

// Arret complet du chemin audio. Speaker.end() ne fait, sur StickS3, que couper la broche d'ampli (GPIO3 du PMIC)
// et desinstaller l'I2S : le DAC de l'ES8311 reste alimente. On le met donc aussi hors tension par I2C, avec les
// memes valeurs que M5Unified (_microphone_enabled_cb_sticks3 / _speaker_enabled_cb_stopwatch). Speaker.begin() et
// Mic.begin() refont un reset complet du codec (reg 0x00 = 0x80) : aucun etat a restaurer.
static void codecPowerDown() {
  static const uint8_t kRegs[][2] = {
      {0x12, 0x02},  // SYSTEM : DAC hors tension
      {0x0D, 0xFC},  // SYSTEM : circuits analogiques hors tension
      {0x0E, 0x6A},  // SYSTEM : PGA / modulateur ADC coupes
      {0x00, 0x00},  // RESET : CSM hors tension
  };
  for (const auto &r : kRegs) M5.In_I2C.writeRegister8(0x18, r[0], r[1], 100000);
}

void speakerOff() {
  if (!sSpkOn) return;  // jamais demarre : rien a couper (ne pas rappeler end() inutilement)
  M5.Speaker.stop();
  M5.Speaker.setVolume(0);  // pas de clic a la coupure de l'ampli
  M5.Speaker.end();         // ampli coupe (PMIC), I2S desinstalle
  sSpkOn = false;
  codecPowerDown();
}

void quiesce() {
  sSpkOn = true;  // force la sequence complete meme si l'etat est inconnu (reveil deep sleep, boot)
  speakerOff();
}

void housekeeping() {
  soundTick();
  if (sSpkOn && !sActive && !M5.Speaker.isPlaying()) speakerOff();
}

uint8_t *recordWav(bool (*isHeld)(), int rate, int maxSeconds, size_t *outLen, void (*onTick)(int)) {
  size_t maxSamples = (size_t)rate * maxSeconds;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(44 + maxSamples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return nullptr;
  int16_t *pcm = (int16_t *)(buf + 44);
  speakerOff();
  if (!M5.Mic.begin()) { heap_caps_free(buf); return nullptr; }
  const size_t chunk = rate / 20;  // 50 ms
  size_t pos = 0;
  uint32_t t0 = millis();
  while (isHeld() && pos < maxSamples) {
    size_t n = chunk < maxSamples - pos ? chunk : maxSamples - pos;
    if (M5.Mic.record(pcm + pos, n, rate)) pos += n;
    else delay(1);
    M5.update();
    if (onTick) onTick((int)(millis() - t0));
  }
  while (M5.Mic.isRecording()) delay(2);
  M5.Mic.end();
  if (pos * 1000 / rate < (size_t)cfg::kRecMinMs) { heap_caps_free(buf); return nullptr; }
  san::buildWavHeader(buf, rate, pos * 2);
  *outLen = 44 + pos * 2;
  return buf;
}

}  // namespace audio_io
