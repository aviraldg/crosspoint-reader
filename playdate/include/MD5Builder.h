#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

class MD5Builder {
 public:
  void begin() {
    state_[0] = 0x67452301U;
    state_[1] = 0xEFCDAB89U;
    state_[2] = 0x98BADCFEU;
    state_[3] = 0x10325476U;
    count_[0] = 0;
    count_[1] = 0;
    std::memset(buffer_, 0, sizeof(buffer_));
    std::memset(digest_, 0, sizeof(digest_));
  }

  void add(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    uint32_t i = (count_[0] >> 3) & 0x3FU;
    const uint32_t bitLen = static_cast<uint32_t>(len << 3);
    count_[0] += bitLen;
    if (count_[0] < bitLen) {
      count_[1]++;
    }
    count_[1] += static_cast<uint32_t>(len >> 29);

    const size_t partLen = 64 - i;
    size_t idx = 0;
    if (len >= partLen) {
      std::memcpy(&buffer_[i], data, partLen);
      transform(buffer_);
      for (idx = partLen; idx + 63 < len; idx += 64) {
        transform(&data[idx]);
      }
      i = 0;
    }
    std::memcpy(&buffer_[i], &data[idx], len - idx);
  }

  void add(const char* data) {
    if (!data) return;
    add(reinterpret_cast<const uint8_t*>(data), std::strlen(data));
  }

  void add(const String& data) { add(reinterpret_cast<const uint8_t*>(data.c_str()), data.length()); }

  void calculate() {
    static constexpr uint8_t kPadding[64] = {0x80};
    uint8_t bits[8];
    encode(bits, count_, 8);

    const uint32_t index = (count_[0] >> 3) & 0x3FU;
    const uint32_t padLen = (index < 56) ? (56 - index) : (120 - index);
    add(kPadding, padLen);
    add(bits, 8);
    encode(digest_, state_, 16);
  }

  void getBytes(uint8_t* output) const {
    if (output) {
      std::memcpy(output, digest_, 16);
    }
  }

  void getChars(char* output) const {
    if (!output) return;
    for (int i = 0; i < 16; ++i) {
      std::snprintf(output + (i * 2), 3, "%02x", digest_[i]);
    }
  }

  String toString() const {
    char hex[33];
    getChars(hex);
    return String(hex);
  }

 private:
  static constexpr uint32_t rotateLeft(uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); }

  static void encode(uint8_t* output, const uint32_t* input, size_t len) {
    for (size_t i = 0, j = 0; j < len; ++i, j += 4) {
      output[j] = static_cast<uint8_t>(input[i] & 0xFFU);
      output[j + 1] = static_cast<uint8_t>((input[i] >> 8) & 0xFFU);
      output[j + 2] = static_cast<uint8_t>((input[i] >> 16) & 0xFFU);
      output[j + 3] = static_cast<uint8_t>((input[i] >> 24) & 0xFFU);
    }
  }

  static void decode(uint32_t* output, const uint8_t* input, size_t len) {
    for (size_t i = 0, j = 0; j < len; ++i, j += 4) {
      output[i] = static_cast<uint32_t>(input[j]) | (static_cast<uint32_t>(input[j + 1]) << 8) |
                  (static_cast<uint32_t>(input[j + 2]) << 16) | (static_cast<uint32_t>(input[j + 3]) << 24);
    }
  }

  void transform(const uint8_t block[64]) {
    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t x[16];
    decode(x, block, 64);

    auto ff = [](uint32_t& v, uint32_t x1, uint32_t y, uint32_t z, uint32_t m, uint32_t s, uint32_t ac) {
      v = rotateLeft(v + ((x1 & y) | (~x1 & z)) + m + ac, s) + x1;
    };
    auto gg = [](uint32_t& v, uint32_t x1, uint32_t y, uint32_t z, uint32_t m, uint32_t s, uint32_t ac) {
      v = rotateLeft(v + ((x1 & z) | (y & ~z)) + m + ac, s) + x1;
    };
    auto hh = [](uint32_t& v, uint32_t x1, uint32_t y, uint32_t z, uint32_t m, uint32_t s, uint32_t ac) {
      v = rotateLeft(v + (x1 ^ y ^ z) + m + ac, s) + x1;
    };
    auto ii = [](uint32_t& v, uint32_t x1, uint32_t y, uint32_t z, uint32_t m, uint32_t s, uint32_t ac) {
      v = rotateLeft(v + (y ^ (x1 | ~z)) + m + ac, s) + x1;
    };

    ff(a, b, c, d, x[0], 7, 0xd76aa478U);
    ff(d, a, b, c, x[1], 12, 0xe8c7b756U);
    ff(c, d, a, b, x[2], 17, 0x242070dbU);
    ff(b, c, d, a, x[3], 22, 0xc1bdceeeU);
    ff(a, b, c, d, x[4], 7, 0xf57c0fafU);
    ff(d, a, b, c, x[5], 12, 0x4787c62aU);
    ff(c, d, a, b, x[6], 17, 0xa8304613U);
    ff(b, c, d, a, x[7], 22, 0xfd469501U);
    ff(a, b, c, d, x[8], 7, 0x698098d8U);
    ff(d, a, b, c, x[9], 12, 0x8b44f7afU);
    ff(c, d, a, b, x[10], 17, 0xffff5bb1U);
    ff(b, c, d, a, x[11], 22, 0x895cd7beU);
    ff(a, b, c, d, x[12], 7, 0x6b901122U);
    ff(d, a, b, c, x[13], 12, 0xfd987193U);
    ff(c, d, a, b, x[14], 17, 0xa679438eU);
    ff(b, c, d, a, x[15], 22, 0x49b40821U);

    gg(a, b, c, d, x[1], 5, 0xf61e2562U);
    gg(d, a, b, c, x[6], 9, 0xc040b340U);
    gg(c, d, a, b, x[11], 14, 0x265e5a51U);
    gg(b, c, d, a, x[0], 20, 0xe9b6c7aaU);
    gg(a, b, c, d, x[5], 5, 0xd62f105dU);
    gg(d, a, b, c, x[10], 9, 0x02441453U);
    gg(c, d, a, b, x[15], 14, 0xd8a1e681U);
    gg(b, c, d, a, x[4], 20, 0xe7d3fbc8U);
    gg(a, b, c, d, x[9], 5, 0x21e1cde6U);
    gg(d, a, b, c, x[14], 9, 0xc33707d6U);
    gg(c, d, a, b, x[3], 14, 0xf4d50d87U);
    gg(b, c, d, a, x[8], 20, 0x455a14edU);
    gg(a, b, c, d, x[13], 5, 0xa9e3e905U);
    gg(d, a, b, c, x[2], 9, 0xfcefa3f8U);
    gg(c, d, a, b, x[7], 14, 0x676f02d9U);
    gg(b, c, d, a, x[12], 20, 0x8d2a4c8aU);

    hh(a, b, c, d, x[5], 4, 0xfffa3942U);
    hh(d, a, b, c, x[8], 11, 0x8771f681U);
    hh(c, d, a, b, x[11], 16, 0x6d9d6122U);
    hh(b, c, d, a, x[14], 23, 0xfde5380cU);
    hh(a, b, c, d, x[1], 4, 0xa4beea44U);
    hh(d, a, b, c, x[4], 11, 0x4bdecfa9U);
    hh(c, d, a, b, x[7], 16, 0xf6bb4b60U);
    hh(b, c, d, a, x[10], 23, 0xbebfbc70U);
    hh(a, b, c, d, x[13], 4, 0x289b7ec6U);
    hh(d, a, b, c, x[0], 11, 0xeaa127faU);
    hh(c, d, a, b, x[3], 16, 0xd4ef3085U);
    hh(b, c, d, a, x[6], 23, 0x04881d05U);
    hh(a, b, c, d, x[9], 4, 0xd9d4d039U);
    hh(d, a, b, c, x[12], 11, 0xe6db99e5U);
    hh(c, d, a, b, x[15], 16, 0x1fa27cf8U);
    hh(b, c, d, a, x[2], 23, 0xc4ac5665U);

    ii(a, b, c, d, x[0], 6, 0xf4292244U);
    ii(d, a, b, c, x[7], 10, 0x432aff97U);
    ii(c, d, a, b, x[14], 15, 0xab9423a7U);
    ii(b, c, d, a, x[5], 21, 0xfc93a039U);
    ii(a, b, c, d, x[12], 6, 0x655b59c3U);
    ii(d, a, b, c, x[3], 10, 0x8f0ccc92U);
    ii(c, d, a, b, x[10], 15, 0xffeff47dU);
    ii(b, c, d, a, x[1], 21, 0x85845dd1U);
    ii(a, b, c, d, x[8], 6, 0x6fa87e4fU);
    ii(d, a, b, c, x[15], 10, 0xfe2ce6e0U);
    ii(c, d, a, b, x[6], 15, 0xa3014314U);
    ii(b, c, d, a, x[13], 21, 0x4e0811a1U);
    ii(a, b, c, d, x[4], 6, 0xf7537e82U);
    ii(d, a, b, c, x[11], 10, 0xbd3af235U);
    ii(c, d, a, b, x[2], 15, 0x2ad7d2bbU);
    ii(b, c, d, a, x[9], 21, 0xeb86d391U);

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
  }

  uint32_t state_[4] = {};
  uint32_t count_[2] = {};
  uint8_t buffer_[64] = {};
  uint8_t digest_[16] = {};
};
