#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

inline uint32_t esp_random() {
  static uint32_t state = 0x5044524EU;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state ^ static_cast<uint32_t>(std::rand());
}

inline void esp_fill_random(void* buf, size_t len) {
  if (!buf) return;
  auto* out = static_cast<uint8_t*>(buf);
  for (size_t i = 0; i < len; ++i) {
    out[i] = static_cast<uint8_t>(esp_random() & 0xFFU);
  }
}
