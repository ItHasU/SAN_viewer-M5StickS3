// Lecture de clips WAV (PCM s16le / IMA ADPCM), tons de nudge, enregistrement du micro.
#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_io {
// Interrompt la lecture si abortOnButton et qu'un bouton est presse. Renvoie false si format non supporte.
bool playWav(const uint8_t *data, size_t len, bool abortOnButton = true);
void nudgeTone(bool high);
void speakerOff();

// Enregistre tant que isHeld() est vrai (max maxSamples). Renvoie un buffer PSRAM : en-tete WAV (44) + PCM.
// Libere avec heap_caps_free. outLen = taille totale. nullptr si trop court / erreur.
uint8_t *recordWav(bool (*isHeld)(), int rate, int maxSeconds, size_t *outLen, void (*onTick)(int ms));
}  // namespace audio_io
