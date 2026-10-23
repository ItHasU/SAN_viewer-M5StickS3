// SHA-256 autonome (sans dependance), utilise pour le hash des capacites.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace san {

struct Sha256Ctx {
  uint32_t h[8];
  uint8_t buf[64];
  uint64_t total;
  size_t fill;
};

void sha256Init(Sha256Ctx &c);
void sha256Update(Sha256Ctx &c, const uint8_t *data, size_t len);
void sha256Final(Sha256Ctx &c, uint8_t out[32]);

// Hex minuscule complet (64 caracteres).
std::string sha256Hex(const std::string &data);
// N premiers caracteres hex (ex. 16) - format "caps_hash" de DESIGN.md section 4.
std::string sha256HexTrunc(const std::string &data, size_t nChars);

}  // namespace san
