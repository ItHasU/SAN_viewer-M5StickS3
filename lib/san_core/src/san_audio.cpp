#include "san_audio.h"

#include <cstring>

namespace san {
namespace {
uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int16_t kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

inline int16_t step(int &pred, int &idx, uint8_t nib) {
  int s = kStepTable[idx];
  int diff = s >> 3;
  if (nib & 4) diff += s;
  if (nib & 2) diff += s >> 1;
  if (nib & 1) diff += s >> 2;
  pred += (nib & 8) ? -diff : diff;
  if (pred > 32767) pred = 32767;
  if (pred < -32768) pred = -32768;
  idx += kIndexTable[nib];
  if (idx < 0) idx = 0;
  if (idx > 88) idx = 88;
  return (int16_t)pred;
}
}  // namespace

WavInfo parseWav(const uint8_t *d, size_t len) {
  WavInfo w;
  if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return w;
  size_t p = 12;
  bool haveFmt = false;
  while (p + 8 <= len) {
    uint32_t sz = rd32(d + p + 4);
    const uint8_t *c = d + p + 8;
    if (!memcmp(d + p, "fmt ", 4) && sz >= 16 && p + 8 + 16 <= len) {
      w.format = rd16(c);
      w.channels = rd16(c + 2);
      w.sampleRate = rd32(c + 4);
      w.blockAlign = rd16(c + 12);
      w.bits = rd16(c + 14);
      if (w.format == 0x11 && sz >= 20 && p + 8 + 20 <= len) w.samplesPerBlock = rd16(c + 18);
      if (w.format == 0x11 && w.samplesPerBlock == 0 && w.blockAlign > 4)
        w.samplesPerBlock = (w.blockAlign - 4) * 2 + 1;
      haveFmt = true;
    } else if (!memcmp(d + p, "fact", 4) && sz >= 4 && p + 8 + 4 <= len) {
      w.factSamples = rd32(c);
    } else if (!memcmp(d + p, "data", 4)) {
      if (!haveFmt) return w;
      w.dataOffset = p + 8;
      size_t avail = len - w.dataOffset;
      w.dataLen = (sz > avail) ? avail : sz;  // gere aussi 0xFFFFFFFF (flux ffmpeg)
      w.valid = (w.format == 1 || w.format == 0x11) && w.channels >= 1;
      return w;
    }
    size_t adv = 8 + (size_t)sz + (sz & 1);
    if (adv > len - p) break;
    p += adv;
  }
  return w;
}

size_t imaSampleCount(const WavInfo &w) {
  if (w.format != 0x11 || w.blockAlign <= 4 || w.channels != 1) return 0;
  size_t full = w.dataLen / w.blockAlign, rem = w.dataLen % w.blockAlign;
  size_t n = full * w.samplesPerBlock;
  if (rem > 4) n += 1 + (rem - 4) * 2;
  if (w.factSamples > 0 && w.factSamples < n) n = w.factSamples;  // ignore le remplissage du dernier bloc
  return n;
}

size_t imaDecode(const WavInfo &w, const uint8_t *data, int16_t *out, size_t cap) {
  if (w.format != 0x11 || w.channels != 1 || w.blockAlign <= 4) return 0;
  size_t n = 0;
  size_t off = 0;
  const size_t total = imaSampleCount(w);
  if (cap > total) cap = total;
  while (off + 4 < w.dataLen && n < cap) {
    size_t bl = w.dataLen - off < w.blockAlign ? w.dataLen - off : w.blockAlign;
    if (bl <= 4) break;
    const uint8_t *b = data + w.dataOffset + off;
    int pred = (int16_t)rd16(b);
    int idx = b[2] > 88 ? 88 : b[2];
    if (n < cap) out[n++] = (int16_t)pred;
    for (size_t i = 4; i < bl; i++) {
      if (n < cap) out[n++] = step(pred, idx, b[i] & 0x0F);
      if (n < cap) out[n++] = step(pred, idx, b[i] >> 4);
    }
    off += bl;
  }
  return n;
}

void buildWavHeader(uint8_t h[44], uint32_t rate, uint32_t dataBytes) {
  memcpy(h, "RIFF", 4); wr32(h + 4, 36 + dataBytes); memcpy(h + 8, "WAVEfmt ", 8);
  wr32(h + 16, 16); wr16(h + 20, 1); wr16(h + 22, 1); wr32(h + 24, rate);
  wr32(h + 28, rate * 2); wr16(h + 32, 2); wr16(h + 34, 16);
  memcpy(h + 36, "data", 4); wr32(h + 40, dataBytes);
}

int micGainQ8(const int16_t *pcm, size_t n) {
  int peak = 0;
  for (size_t i = 0; i < n; i++) {
    int v = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
    if (v > peak) peak = v;
  }
  if (peak == 0) return kMicGainQ8;
  long g = (long)kMicTarget * 256 / peak;
  if (g > kMicGainQ8) g = kMicGainQ8;
  if (g < 256) g = 256;
  return (int)g;
}

void applyGainQ8(int16_t *pcm, size_t n, int gainQ8) {
  if (gainQ8 == 256) return;
  for (size_t i = 0; i < n; i++) {
    long v = ((long)pcm[i] * gainQ8) >> 8;
    pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
  }
}

void applyFades(int16_t *pcm, size_t n, uint32_t rate) {
  size_t fi = (size_t)rate * kMicFadeInMs / 1000, fo = (size_t)rate * kMicFadeOutMs / 1000;
  if (fi + fo > n) { fi = n / 3; fo = n / 3; }
  for (size_t i = 0; i < fi; i++) pcm[i] = (int16_t)((long)pcm[i] * (long)i / (long)fi);
  for (size_t i = 0; i < fo; i++) pcm[n - 1 - i] = (int16_t)((long)pcm[n - 1 - i] * (long)i / (long)fo);
}

}  // namespace san
