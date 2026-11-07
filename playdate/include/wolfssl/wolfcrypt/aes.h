#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef INVALID_DEVID
#define INVALID_DEVID (-2)
#endif

struct Aes {
  uint8_t key[32]{};
  size_t keyLen = 0;
};

inline int wc_AesInit(Aes* aes, void* heap, int devId) {
  (void)heap;
  (void)devId;
  if (!aes) return -1;
  std::memset(aes, 0, sizeof(*aes));
  return 0;
}

inline void wc_AesFree(Aes* aes) {
  if (aes) {
    std::memset(aes, 0, sizeof(*aes));
  }
}

inline int wc_AesGcmSetKey(Aes* aes, const uint8_t* key, uint32_t len) {
  if (!aes || !key || (len != 16 && len != 32)) return -1;
  std::memcpy(aes->key, key, len);
  aes->keyLen = len;
  return 0;
}

inline int wc_AesGcmEncrypt(Aes* aes, uint8_t* out, const uint8_t* in, uint32_t sz, const uint8_t* iv, uint32_t ivSz,
                            uint8_t* authTag, uint32_t authTagSz, const uint8_t* authIn, uint32_t authInSz) {
  (void)aes;
  (void)out;
  (void)in;
  (void)sz;
  (void)iv;
  (void)ivSz;
  (void)authTag;
  (void)authTagSz;
  (void)authIn;
  (void)authInSz;
  return -1;
}

inline int wc_AesGcmDecrypt(Aes* aes, uint8_t* out, const uint8_t* in, uint32_t sz, const uint8_t* iv, uint32_t ivSz,
                            const uint8_t* authTag, uint32_t authTagSz, const uint8_t* authIn, uint32_t authInSz) {
  (void)aes;
  (void)out;
  (void)in;
  (void)sz;
  (void)iv;
  (void)ivSz;
  (void)authTag;
  (void)authTagSz;
  (void)authIn;
  (void)authInSz;
  return -1;
}
