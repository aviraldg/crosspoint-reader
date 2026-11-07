#pragma once

#include <algorithm>
#include <cmath>
#include <cstdbool>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HardwareSerial.h"
#include "Print.h"
#include "WString.h"
#include "esp_system.h"

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef PGM_P
#define PGM_P const char*
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(reinterpret_cast<const uint8_t*>(addr)))
#endif

#ifndef pgm_read_word
#define pgm_read_word(addr) (*(reinterpret_cast<const uint16_t*>(addr)))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(reinterpret_cast<const uint32_t*>(addr)))
#endif

#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(reinterpret_cast<const void* const*>(addr)))
#endif

#ifndef memcpy_P
#define memcpy_P(dest, src, num) std::memcpy((dest), (src), (num))
#endif

#ifndef strncpy_P
#define strncpy_P(dest, src, num) std::strncpy((dest), (src), (num))
#endif

#ifndef strcmp_P
#define strcmp_P(a, b) std::strcmp((a), (b))
#endif

#ifndef strlen_P
#define strlen_P(s) std::strlen((s))
#endif

#ifndef LOW
#define LOW 0x0
#endif
#ifndef HIGH
#define HIGH 0x1
#endif

#ifndef INPUT
#define INPUT 0x01
#endif
#ifndef OUTPUT
#define OUTPUT 0x03
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x05
#endif
#ifndef INPUT_PULLDOWN
#define INPUT_PULLDOWN 0x09
#endif

using byte = uint8_t;
using word = uint16_t;
using boolean = bool;

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

inline void pinMode(uint8_t pin, uint8_t mode) {
  (void)pin;
  (void)mode;
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
  (void)pin;
  (void)val;
}

inline int digitalRead(uint8_t pin) {
  (void)pin;
  return LOW;
}

inline int analogRead(uint8_t pin) {
  (void)pin;
  return 0;
}

inline uint32_t getCpuFrequencyMhz() { return 168; }
inline bool setCpuFrequencyMhz(uint32_t freqMhz) {
  (void)freqMhz;
  return true;
}

inline long random(long howbig) {
  if (howbig <= 0) return 0;
  return std::rand() % howbig;
}

inline long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  return random(howbig - howsmall) + howsmall;
}
