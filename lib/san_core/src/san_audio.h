// Parsing WAV, decodage IMA ADPCM (WAV 0x11, mono), en-tete WAV PCM pour l'enregistrement.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace san {

struct WavInfo {
  bool valid = false;
  uint16_t format = 0;  // 1 = PCM, 0x11 = IMA ADPCM
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t blockAlign = 0;
  uint16_t bits = 0;
  uint16_t samplesPerBlock = 0;
  size_t dataOffset = 0;
  size_t dataLen = 0;  // borne par la taille reellement disponible
  uint32_t factSamples = 0;  // chunk 'fact' (ffmpeg l'ecrit pour l'ADPCM) : vrai nombre d'echantillons, 0 si absent
};

WavInfo parseWav(const uint8_t *data, size_t len);
// Nombre d'echantillons mono produits pour dataLen octets d'ADPCM (borne par 'fact' : le dernier bloc est complete par l'encodeur).
size_t imaSampleCount(const WavInfo &w);
// Decode de l'IMA ADPCM mono vers PCM s16. Renvoie le nombre d'echantillons ecrits.
size_t imaDecode(const WavInfo &w, const uint8_t *data, int16_t *out, size_t outCap);
// En-tete WAV PCM s16le mono de 44 octets.
void buildWavHeader(uint8_t out[44], uint32_t sampleRate, uint32_t dataBytes);

// Gain micro (Q8 : 256 = x1) : +12 dB (x4 = 1024) tant que le pic reste sous kMicCeiling, sinon le plus grand gain
// qui ne sature pas (jamais en dessous de x1). Silence : x4.
constexpr int kMicGainQ8 = 1024;
constexpr int kMicCeiling = 32000;
int micGainQ8(const int16_t *pcm, size_t n);
void applyGainQ8(int16_t *pcm, size_t n, int gainQ8);  // saturation par securite

}  // namespace san
