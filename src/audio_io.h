// Lecture de clips WAV (PCM s16le / IMA ADPCM), tons de nudge, enregistrement du micro.
#pragma once
#include <cstddef>
#include <cstdint>

#include "san_types.h"

namespace audio_io {
// Volume 0-255 applique immediatement (0 : aucun son). Memorise pour les lectures suivantes.
void setVolume(int v);
// Bip court d'apercu (reglage du volume), non bloquant.
void beep();
// Interrompt la lecture si abortOnButton et qu'un bouton est presse. Renvoie false si format non supporte.
bool playWav(const uint8_t *data, size_t len, bool abortOnButton = true);
// Son de nudge non bloquant : soundStart() lance, soundTick() doit etre appele regulierement,
// soundWait() bloque jusqu'a la fin (avant le deep sleep).
void soundStart(san::NudgeType type, bool high);
void soundTick();
bool soundBusy();
void soundWait();
void soundStop();
void speakerOff();
// Libere le haut-parleur quand plus rien ne joue (appeler regulierement, ex. depuis la boucle principale).
void housekeeping();

// Enregistre tant que isHeld() est vrai (max maxSamples). Renvoie un buffer PSRAM : en-tete WAV (44) + PCM.
// Libere avec heap_caps_free. outLen = taille totale. nullptr si trop court / erreur.
uint8_t *recordWav(bool (*isHeld)(), int rate, int maxSeconds, size_t *outLen, void (*onTick)(int ms));
}  // namespace audio_io
