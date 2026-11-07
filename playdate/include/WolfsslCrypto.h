#pragma once

#include <Crypto.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "esp_random.h"

namespace freeink {
namespace content {

class WolfsslCrypto : public Crypto {
 public:
  WolfsslCrypto() = default;
  ~WolfsslCrypto() override = default;

  std::string lastError;

  int32_t rsaPrivateRaw(const uint8_t* pkcs8Der, size_t pkcs8Len, const uint8_t* in, size_t inLen, uint8_t* out,
                        size_t outCap) override {
    (void)pkcs8Der;
    (void)pkcs8Len;
    (void)in;
    (void)inLen;
    (void)out;
    (void)outCap;
    lastError = "RSA unsupported on Playdate";
    return -1;
  }

  bool aes128CbcDecrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t len,
                        uint8_t* out) override {
    (void)key;
    (void)iv;
    (void)in;
    (void)len;
    (void)out;
    lastError = "AES-128-CBC unsupported on Playdate";
    return false;
  }

  bool aes256CbcDecrypt(const uint8_t key[32], const uint8_t iv[16], const uint8_t* in, size_t len,
                        uint8_t* out) override {
    (void)key;
    (void)iv;
    (void)in;
    (void)len;
    (void)out;
    lastError = "AES-256-CBC unsupported on Playdate";
    return false;
  }

  bool x25519MakeKey(uint8_t priv[32], uint8_t pub[32]) override {
    (void)priv;
    (void)pub;
    lastError = "X25519 unsupported on Playdate";
    return false;
  }

  bool x25519SharedSecret(const uint8_t priv[32], const uint8_t peerPub[32], uint8_t out[32]) override {
    (void)priv;
    (void)peerPub;
    (void)out;
    lastError = "X25519 unsupported on Playdate";
    return false;
  }

  bool hkdfSha256(const uint8_t* ikm, size_t ikmLen, const uint8_t* info, size_t infoLen, uint8_t* out,
                  size_t outLen) override {
    (void)ikm;
    (void)ikmLen;
    (void)info;
    (void)infoLen;
    (void)out;
    (void)outLen;
    lastError = "HKDF unsupported on Playdate";
    return false;
  }

  bool aes256GcmDecrypt(const uint8_t key[32], const uint8_t iv[12], const uint8_t* in, size_t len,
                        const uint8_t tag[16], uint8_t* out) override {
    (void)key;
    (void)iv;
    (void)in;
    (void)len;
    (void)tag;
    (void)out;
    lastError = "AES-256-GCM unsupported on Playdate";
    return false;
  }

  void sha1(const uint8_t* data, size_t len, uint8_t out[20]) override {
    uint32_t h0 = 0x67452301U;
    uint32_t h1 = 0xEFCDAB89U;
    uint32_t h2 = 0x98BADCFEU;
    uint32_t h3 = 0x10325476U;
    uint32_t h4 = 0xC3D2E1F0U;

    const uint64_t bitLen = static_cast<uint64_t>(len) * 8ULL;
    size_t paddedLen = ((len + 8) / 64 + 1) * 64;
    std::vector<uint8_t> msg(paddedLen, 0);
    if (data && len > 0) {
      std::memcpy(msg.data(), data, len);
    }
    msg[len] = 0x80U;
    for (int i = 0; i < 8; ++i) {
      msg[paddedLen - 1 - i] = static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFFU);
    }

    auto rol = [](uint32_t v, uint32_t b) { return (v << b) | (v >> (32 - b)); };

    for (size_t offset = 0; offset < paddedLen; offset += 64) {
      uint32_t w[80];
      for (int i = 0; i < 16; ++i) {
        const uint8_t* p = &msg[offset + i * 4];
        w[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
      }
      for (int i = 16; i < 80; ++i) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
      }

      uint32_t a = h0;
      uint32_t b = h1;
      uint32_t c = h2;
      uint32_t d = h3;
      uint32_t e = h4;

      for (int i = 0; i < 80; ++i) {
        uint32_t f = 0;
        uint32_t k = 0;
        if (i < 20) {
          f = (b & c) | ((~b) & d);
          k = 0x5A827999U;
        } else if (i < 40) {
          f = b ^ c ^ d;
          k = 0x6ED9EBA1U;
        } else if (i < 60) {
          f = (b & c) | (b & d) | (c & d);
          k = 0x8F1BBCDCU;
        } else {
          f = b ^ c ^ d;
          k = 0xCA62C1D6U;
        }
        const uint32_t temp = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = temp;
      }

      h0 += a;
      h1 += b;
      h2 += c;
      h3 += d;
      h4 += e;
    }

    const uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
      out[i * 4 + 0] = static_cast<uint8_t>((hs[i] >> 24) & 0xFFU);
      out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFFU);
      out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFFU);
      out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFFU);
    }
  }

  void sha256(const uint8_t* data, size_t len, uint8_t out[32]) override {
    static constexpr uint32_t kTable[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

    uint32_t h[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                     0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    const uint64_t bitLen = static_cast<uint64_t>(len) * 8ULL;
    const size_t paddedLen = ((len + 8) / 64 + 1) * 64;
    std::vector<uint8_t> msg(paddedLen, 0);
    if (data && len > 0) {
      std::memcpy(msg.data(), data, len);
    }
    msg[len] = 0x80U;
    for (int i = 0; i < 8; ++i) {
      msg[paddedLen - 1 - i] = static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFFU);
    }

    auto ror = [](uint32_t v, uint32_t b) { return (v >> b) | (v << (32 - b)); };

    for (size_t offset = 0; offset < paddedLen; offset += 64) {
      uint32_t w[64];
      for (int i = 0; i < 16; ++i) {
        const uint8_t* p = &msg[offset + i * 4];
        w[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
      }
      for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
      }

      uint32_t a = h[0];
      uint32_t b = h[1];
      uint32_t c = h[2];
      uint32_t d = h[3];
      uint32_t e = h[4];
      uint32_t f = h[5];
      uint32_t g = h[6];
      uint32_t hh = h[7];

      for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = hh + s1 + ch + kTable[i] + w[i];
        const uint32_t s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        hh = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
      }

      h[0] += a;
      h[1] += b;
      h[2] += c;
      h[3] += d;
      h[4] += e;
      h[5] += f;
      h[6] += g;
      h[7] += hh;
    }

    for (int i = 0; i < 8; ++i) {
      out[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xFFU);
      out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFFU);
      out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFFU);
      out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFFU);
    }
  }

  bool rsaGenerate(RsaKeyPairDer* out) override {
    (void)out;
    lastError = "RSA keygen unsupported on Playdate";
    return false;
  }

  bool rsaPublicEncrypt(const uint8_t* certDer, size_t certLen, const uint8_t* in, size_t inLen, uint8_t* out,
                        size_t outCap, size_t* outLen) override {
    (void)certDer;
    (void)certLen;
    (void)in;
    (void)inLen;
    (void)out;
    (void)outCap;
    (void)outLen;
    lastError = "RSA encrypt unsupported on Playdate";
    return false;
  }

  bool rsaPrivateSignRaw(const uint8_t* pkcs8Der, size_t pkcs8Len, const uint8_t hash[20], uint8_t out[128]) override {
    (void)pkcs8Der;
    (void)pkcs8Len;
    (void)hash;
    (void)out;
    lastError = "RSA sign unsupported on Playdate";
    return false;
  }

  bool aes128CbcEncrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t len,
                        uint8_t* out) override {
    (void)key;
    (void)iv;
    (void)in;
    (void)len;
    (void)out;
    lastError = "AES-128-CBC encrypt unsupported on Playdate";
    return false;
  }

  bool pkcs12Extract(const uint8_t* p12, size_t len, const std::string& password, std::vector<uint8_t>* keyPkcs8,
                     std::vector<uint8_t>* certDer) override {
    (void)p12;
    (void)len;
    (void)password;
    (void)keyPkcs8;
    (void)certDer;
    lastError = "PKCS#12 unsupported on Playdate";
    return false;
  }

  void randomBytes(uint8_t* out, size_t len) override { esp_fill_random(out, len); }
};

}  // namespace content
}  // namespace freeink
