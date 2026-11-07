#pragma once

#include "Print.h"

class HardwareSerial : public Print {
 public:
  void begin(unsigned long baud = 115200) { (void)baud; }
  void end() {}
  int available() { return 0; }
  int read() { return -1; }
  int peek() { return -1; }
  void flush() override {}
  operator bool() const { return true; }

  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};

using HWCDC = HardwareSerial;

extern HardwareSerial Serial;
