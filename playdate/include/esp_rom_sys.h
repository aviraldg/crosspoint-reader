#pragma once

#include <cstdint>

inline void esp_rom_delay_us(uint32_t us) { (void)us; }
inline int esp_rom_printf(const char* fmt, ...) {
  (void)fmt;
  return 0;
}

