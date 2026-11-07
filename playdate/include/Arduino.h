#pragma once

#ifndef CROSSPOINT_PLAYDATE
#define CROSSPOINT_PLAYDATE 1
#endif
#ifndef CROSSPOINT_CUSTOM_DISPLAY_HAL
#define CROSSPOINT_CUSTOM_DISPLAY_HAL 1
#endif
#ifndef CROSSPOINT_DISPLAY_WIDTH
#define CROSSPOINT_DISPLAY_WIDTH 400
#endif
#ifndef CROSSPOINT_DISPLAY_HEIGHT
#define CROSSPOINT_DISPLAY_HEIGHT 240
#endif
#ifndef CROSSPOINT_DEFAULT_UI_ORIENTATION
#define CROSSPOINT_DEFAULT_UI_ORIENTATION GfxRenderer::LandscapeCounterClockwise
#endif
#ifndef CROSSPOINT_DEFAULT_UI_LANDSCAPE_CCW
#define CROSSPOINT_DEFAULT_UI_LANDSCAPE_CCW 1
#endif
#ifndef CROSSPOINT_COOPERATIVE_RENDER
#define CROSSPOINT_COOPERATIVE_RENDER 1
#endif
#ifndef CROSSPOINT_CAP_LOCAL_TRANSFER
#define CROSSPOINT_CAP_LOCAL_TRANSFER 0
#endif
#ifndef CROSSPOINT_CAP_OTA
#define CROSSPOINT_CAP_OTA 0
#endif
#ifndef FREEINK_CAP_BLE_HID_HOST
#define FREEINK_CAP_BLE_HID_HOST 0
#endif
#ifndef FREEINK_CAP_USB_MSC
#define FREEINK_CAP_USB_MSC 0
#endif
#ifndef FREEINK_DEVICE_X4
#define FREEINK_DEVICE_X4 1
#endif
#ifndef ARDUINOJSON_ENABLE_ARDUINO_STRING
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#endif
#ifndef ARDUINOJSON_ENABLE_ARDUINO_STREAM
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 1
#endif
#ifndef ARDUINOJSON_ENABLE_ARDUINO_PRINT
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 1
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#if !defined(_GLIBCXX_HAS_GTHREADS)
namespace std {
class mutex {
 public:
  constexpr mutex() noexcept = default;
  ~mutex() = default;
  mutex(const mutex&) = delete;
  mutex& operator=(const mutex&) = delete;
  void lock() noexcept {}
  bool try_lock() noexcept { return true; }
  void unlock() noexcept {}
};
class recursive_mutex {
 public:
  constexpr recursive_mutex() noexcept = default;
  ~recursive_mutex() = default;
  recursive_mutex(const recursive_mutex&) = delete;
  recursive_mutex& operator=(const recursive_mutex&) = delete;
  void lock() noexcept {}
  bool try_lock() noexcept { return true; }
  void unlock() noexcept {}
};
}  // namespace std
#endif

using std::ptrdiff_t;
using std::size_t;
using std::time_t;

#include "HardwareSerial.h"
#include "Print.h"
#include "WString.h"
#include "esp_system.h"

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
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
