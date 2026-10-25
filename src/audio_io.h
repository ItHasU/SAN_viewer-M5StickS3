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
// Son de nudge marimba (non bloquant, regles anti-bruit de san::SoundScheduler) : requestSound() enregistre,
// soundTick() (via housekeeping) synthetise et lance, soundWait() bloque jusqu'a la fin (avant le deep sleep).
void requestSound(san::NudgeType type);
void soundTick();
bool soundBusy();
bool soundPending();
void soundWait();
void soundStop();
// Coupe tout le chemin audio (ampli + DAC) ; sans effet si deja coupe. Le haut-parleur n'est demarre qu'a la demande.
void speakerOff();
// Coupe sans condition (boot, avant deep sleep) : l'ES8311 peut etre reste actif apres un reveil.
void quiesce();
// Libere le haut-parleur quand plus rien ne joue (appeler regulierement, ex. depuis la boucle principale).
void housekeeping();

// Enregistre tant que isHeld() est vrai (max maxSamples). Renvoie un buffer PSRAM : en-tete WAV (44) + PCM.
// Libere avec heap_caps_free. outLen = taille totale. nullptr si trop court / erreur.
uint8_t *recordWav(bool (*isHeld)(), int rate, int maxSeconds, size_t *outLen, void (*onTick)(int ms));
}  // namespace audio_io
