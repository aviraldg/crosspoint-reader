#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "WString.h"

namespace base64 {

inline String encode(const uint8_t* data, size_t len) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (!data || len == 0) return String();
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 2 < len) {
    const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                            static_cast<uint32_t>(data[i + 2]);
    out.push_back(kAlphabet[(triple >> 18) & 0x3FU]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3FU]);
    out.push_back(kAlphabet[(triple >> 6) & 0x3FU]);
    out.push_back(kAlphabet[triple & 0x3FU]);
    i += 3;
  }
  if (i < len) {
    uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) {
      triple |= static_cast<uint32_t>(data[i + 1]) << 8;
    }
    out.push_back(kAlphabet[(triple >> 18) & 0x3FU]);
    out.push_back(kAlphabet[(triple >> 12) & 0x3FU]);
    out.push_back((i + 1 < len) ? kAlphabet[(triple >> 6) & 0x3FU] : '=');
    out.push_back('=');
  }
  return String(out);
}

inline String encode(const String& text) {
  return encode(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

}  // namespace base64
