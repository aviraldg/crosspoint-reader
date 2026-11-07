#pragma once

#include <cstddef>
#include <cstdint>

#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef LSBFIRST
#define LSBFIRST 0
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif

class SPISettings {
 public:
  constexpr SPISettings(uint32_t clock = 1000000, uint8_t bitOrder = MSBFIRST, uint8_t dataMode = SPI_MODE0)
      : clock_(clock), bitOrder_(bitOrder), dataMode_(dataMode) {}

 private:
  uint32_t clock_;
  uint8_t bitOrder_;
  uint8_t dataMode_;
};

class SPIClass {
 public:
  void begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1) {
    (void)sck;
    (void)miso;
    (void)mosi;
    (void)ss;
  }
  void end() {}
  void beginTransaction(SPISettings settings) { (void)settings; }
  void endTransaction() {}
  uint8_t transfer(uint8_t data) { return data; }
  void writeBytes(const uint8_t* data, uint32_t size) {
    (void)data;
    (void)size;
  }
};

inline SPIClass SPI;
