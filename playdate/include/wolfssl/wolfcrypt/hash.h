#pragma once

#include <WolfsslCrypto.h>

#include <cstddef>
#include <cstdint>

inline int wc_Sha256Hash(const uint8_t* data, uint32_t len, uint8_t* hash) {
  if (!hash) return -1;
  freeink::content::WolfsslCrypto crypto;
  crypto.sha256(data, len, hash);
  return 0;
}
