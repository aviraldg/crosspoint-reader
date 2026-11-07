#pragma once

#include <cstddef>
#include <cstdint>

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL (-0x002A)
#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER (-0x002C)

inline int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  if (!olen || (!src && slen > 0)) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;

  auto decodeChar = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') return -3;
    return -1;
  };

  size_t validChars = 0;
  size_t padChars = 0;
  for (size_t i = 0; i < slen; ++i) {
    const int v = decodeChar(src[i]);
    if (v == -3) continue;
    if (v == -1) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    if (v == -2) {
      padChars++;
      if (padChars > 2) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    } else if (padChars > 0) {
      return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    }
    validChars++;
  }

  if (validChars % 4 != 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
  const size_t requiredLen = (validChars / 4) * 3 - padChars;
  *olen = requiredLen;
  if (!dst || dlen < requiredLen) {
    return (requiredLen == 0 && dst != nullptr) ? 0 : MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  }

  uint32_t acc = 0;
  int quads = 0;
  size_t outIdx = 0;
  for (size_t i = 0; i < slen; ++i) {
    const int v = decodeChar(src[i]);
    if (v == -3) continue;
    acc = (acc << 6) | static_cast<uint32_t>(v >= 0 ? v : 0);
    quads++;
    if (quads == 4) {
      if (outIdx < requiredLen) dst[outIdx++] = static_cast<unsigned char>((acc >> 16) & 0xFFU);
      if (outIdx < requiredLen) dst[outIdx++] = static_cast<unsigned char>((acc >> 8) & 0xFFU);
      if (outIdx < requiredLen) dst[outIdx++] = static_cast<unsigned char>(acc & 0xFFU);
      acc = 0;
      quads = 0;
    }
  }

  return 0;
}
