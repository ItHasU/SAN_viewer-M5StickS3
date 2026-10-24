#include "audio_io.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "san_audio.h"

namespace audio_io {

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
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(200);  // ~78 % : plus fort que la reco M5Stack (< 75 %), a surveiller (reboot sur batterie ?)
  // playRaw copie par references : le buffer doit rester valide jusqu'a la fin de lecture.
  M5.Speaker.playRaw(pcm, n, w.sampleRate, false, 1, 0);
  waitPlayback(abortOnButton);
  M5.Speaker.stop();
  if (dec) heap_caps_free(dec);
  return true;
}

void nudgeTone(bool high) {
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(200);
  int reps = high ? 3 : 1;
  for (int i = 0; i < reps; i++) {
    M5.Speaker.tone(high ? 1800 : 1200, 120);
    delay(180);
  }
  while (M5.Speaker.isPlaying()) delay(5);
}

void speakerOff() { M5.Speaker.end(); }

uint8_t *recordWav(bool (*isHeld)(), int rate, int maxSeconds, size_t *outLen, void (*onTick)(int)) {
  size_t maxSamples = (size_t)rate * maxSeconds;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(44 + maxSamples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return nullptr;
  int16_t *pcm = (int16_t *)(buf + 44);
  M5.Speaker.end();
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
