#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t b) = 0;
  virtual void flush() {}

  virtual size_t write(const uint8_t* buffer, size_t size) {
    if (!buffer) return 0;
    size_t n = 0;
    while (size--) {
      if (write(*buffer++)) {
        n++;
      } else {
        break;
      }
    }
    return n;
  }

  size_t write(const char* str) {
    if (!str) return 0;
    return write(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
  }

  size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }

  size_t print(const String& s) { return write(s.c_str()); }
  size_t print(const char* str) { return write(str); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(unsigned char b, int base = DEC) { return print(String(b, static_cast<unsigned char>(base))); }
  size_t print(int n, int base = DEC) { return print(String(n, static_cast<unsigned char>(base))); }
  size_t print(unsigned int n, int base = DEC) { return print(String(n, static_cast<unsigned char>(base))); }
  size_t print(long n, int base = DEC) { return print(String(n, static_cast<unsigned char>(base))); }
  size_t print(unsigned long n, int base = DEC) { return print(String(n, static_cast<unsigned char>(base))); }
  size_t print(double n, int digits = 2) { return print(String(n, static_cast<unsigned char>(digits))); }

  size_t println() { return write("\r\n"); }
  size_t println(const String& s) { return print(s) + println(); }
  size_t println(const char* str) { return print(str) + println(); }
  size_t println(char c) { return print(c) + println(); }
  size_t println(unsigned char b, int base = DEC) { return print(b, base) + println(); }
  size_t println(int n, int base = DEC) { return print(n, base) + println(); }
  size_t println(unsigned int n, int base = DEC) { return print(n, base) + println(); }
  size_t println(long n, int base = DEC) { return print(n, base) + println(); }
  size_t println(unsigned long n, int base = DEC) { return print(n, base) + println(); }
  size_t println(double n, int digits = 2) { return print(n, digits) + println(); }

  size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    char stackBuf[256];
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int len = std::vsnprintf(stackBuf, sizeof(stackBuf), format, args);
    va_end(args);
    if (len < 0) {
      va_end(copy);
      return 0;
    }
    if (static_cast<size_t>(len) < sizeof(stackBuf)) {
      va_end(copy);
      return write(reinterpret_cast<const uint8_t*>(stackBuf), static_cast<size_t>(len));
    }
    std::string dynBuf(static_cast<size_t>(len) + 1, '\0');
    std::vsnprintf(dynBuf.data(), dynBuf.size(), format, copy);
    va_end(copy);
    return write(reinterpret_cast<const uint8_t*>(dynBuf.data()), static_cast<size_t>(len));
  }
};

class Stream : public Print {
 public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  virtual size_t readBytes(char* buffer, size_t length) {
    (void)buffer;
    (void)length;
    return 0;
  }
};

class Printable {
 public:
  virtual ~Printable() = default;
  virtual size_t printTo(Print& p) const = 0;
};
