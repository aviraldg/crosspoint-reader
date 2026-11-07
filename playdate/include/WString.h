#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* cstr) : s_(cstr ? cstr : "") {}
  String(const char* cstr, unsigned int length) : s_(cstr ? std::string(cstr, length) : std::string()) {}
  explicit String(const std::string& str) : s_(str) {}
  String(const String&) = default;
  String(String&&) noexcept = default;
  explicit String(char c) : s_(1, c) {}
  explicit String(unsigned char value, unsigned char base = 10) : s_(formatUnsigned(value, base)) {}
  explicit String(int value, unsigned char base = 10) : s_(formatSigned(value, base)) {}
  explicit String(unsigned int value, unsigned char base = 10) : s_(formatUnsigned(value, base)) {}
  explicit String(long value, unsigned char base = 10) : s_(formatSigned(value, base)) {}
  explicit String(unsigned long value, unsigned char base = 10) : s_(formatUnsigned(value, base)) {}
  explicit String(long long value, unsigned char base = 10) : s_(formatSigned(value, base)) {}
  explicit String(unsigned long long value, unsigned char base = 10) : s_(formatUnsigned(value, base)) {}
  explicit String(float value, unsigned char decimalPlaces = 2) : s_(formatFloat(value, decimalPlaces)) {}
  explicit String(double value, unsigned char decimalPlaces = 2) : s_(formatFloat(value, decimalPlaces)) {}

  String& operator=(const String&) = default;
  String& operator=(String&&) noexcept = default;
  String& operator=(const char* cstr) {
    s_ = cstr ? cstr : "";
    return *this;
  }

  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  bool isEmpty() const { return s_.empty(); }
  const char* c_str() const { return s_.c_str(); }
  const std::string& std_str() const { return s_; }

  bool reserve(unsigned int size) {
    s_.reserve(size);
    return true;
  }

  void clear() { s_.clear(); }

  char charAt(unsigned int index) const { return index < s_.size() ? s_[index] : '\0'; }
  void setCharAt(unsigned int index, char c) {
    if (index < s_.size()) {
      s_[index] = c;
    }
  }
  char operator[](unsigned int index) const { return charAt(index); }
  char& operator[](unsigned int index) { return s_[index]; }

  void getBytes(unsigned char* buf, unsigned int bufsize, unsigned int index = 0) const {
    if (!buf || bufsize == 0) return;
    if (index >= s_.size()) {
      buf[0] = 0;
      return;
    }
    const size_t n = std::min<size_t>(bufsize - 1, s_.size() - index);
    std::memcpy(buf, s_.data() + index, n);
    buf[n] = 0;
  }

  void toCharArray(char* buf, unsigned int bufsize, unsigned int index = 0) const {
    getBytes(reinterpret_cast<unsigned char*>(buf), bufsize, index);
  }

  bool concat(const String& str) {
    s_ += str.s_;
    return true;
  }
  bool concat(const char* cstr) {
    if (!cstr) return false;
    s_ += cstr;
    return true;
  }
  bool concat(const char* cstr, unsigned int length) {
    if (!cstr) return false;
    s_.append(cstr, length);
    return true;
  }
  bool concat(char c) {
    s_.push_back(c);
    return true;
  }
  bool concat(int num) {
    s_ += formatSigned(num, 10);
    return true;
  }
  bool concat(unsigned int num) {
    s_ += formatUnsigned(num, 10);
    return true;
  }
  bool concat(long num) {
    s_ += formatSigned(num, 10);
    return true;
  }
  bool concat(unsigned long num) {
    s_ += formatUnsigned(num, 10);
    return true;
  }

  String& operator+=(const String& rhs) {
    concat(rhs);
    return *this;
  }
  String& operator+=(const char* cstr) {
    concat(cstr);
    return *this;
  }
  String& operator+=(char c) {
    concat(c);
    return *this;
  }

  friend String operator+(const String& lhs, const String& rhs) {
    String res(lhs);
    res += rhs;
    return res;
  }
  friend String operator+(const String& lhs, const char* rhs) {
    String res(lhs);
    res += rhs;
    return res;
  }
  friend String operator+(const char* lhs, const String& rhs) {
    String res(lhs);
    res += rhs;
    return res;
  }
  friend String operator+(const String& lhs, char rhs) {
    String res(lhs);
    res += rhs;
    return res;
  }

  int compareTo(const String& s) const { return s_.compare(s.s_); }
  bool equals(const String& s) const { return s_ == s.s_; }
  bool equals(const char* cstr) const { return cstr && s_ == cstr; }
  bool operator==(const String& rhs) const { return equals(rhs); }
  bool operator==(const char* cstr) const { return equals(cstr); }
  bool operator!=(const String& rhs) const { return !equals(rhs); }
  bool operator!=(const char* cstr) const { return !equals(cstr); }
  bool operator<(const String& rhs) const { return s_ < rhs.s_; }
  bool operator>(const String& rhs) const { return s_ > rhs.s_; }
  bool operator<=(const String& rhs) const { return s_ <= rhs.s_; }
  bool operator>=(const String& rhs) const { return s_ >= rhs.s_; }

  bool equalsIgnoreCase(const String& s) const {
    if (s_.size() != s.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(s_[i])) != std::tolower(static_cast<unsigned char>(s.s_[i]))) {
        return false;
      }
    }
    return true;
  }

  bool startsWith(const String& prefix, unsigned int offset = 0) const {
    if (offset > s_.size() || prefix.s_.size() > s_.size() - offset) return false;
    return s_.compare(offset, prefix.s_.size(), prefix.s_) == 0;
  }

  bool endsWith(const String& suffix) const {
    if (suffix.s_.size() > s_.size()) return false;
    return s_.compare(s_.size() - suffix.s_.size(), suffix.s_.size(), suffix.s_) == 0;
  }

  int indexOf(char ch, unsigned int fromIndex = 0) const {
    if (fromIndex >= s_.size()) return -1;
    const size_t pos = s_.find(ch, fromIndex);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  int indexOf(const String& str, unsigned int fromIndex = 0) const {
    if (fromIndex >= s_.size()) return -1;
    const size_t pos = s_.find(str.s_, fromIndex);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  int lastIndexOf(char ch) const {
    const size_t pos = s_.rfind(ch);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  int lastIndexOf(char ch, unsigned int fromIndex) const {
    if (s_.empty()) return -1;
    const size_t pos = s_.rfind(ch, fromIndex);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  int lastIndexOf(const String& str) const {
    const size_t pos = s_.rfind(str.s_);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  int lastIndexOf(const String& str, unsigned int fromIndex) const {
    if (s_.empty()) return -1;
    const size_t pos = s_.rfind(str.s_, fromIndex);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  String substring(unsigned int beginIndex) const {
    return substring(beginIndex, static_cast<unsigned int>(s_.size()));
  }

  String substring(unsigned int beginIndex, unsigned int endIndex) const {
    if (beginIndex > endIndex) std::swap(beginIndex, endIndex);
    if (beginIndex >= s_.size()) return String();
    if (endIndex > s_.size()) endIndex = static_cast<unsigned int>(s_.size());
    return String(s_.substr(beginIndex, endIndex - beginIndex));
  }

  void replace(char find, char replaceWith) { std::replace(s_.begin(), s_.end(), find, replaceWith); }

  void replace(const String& find, const String& replaceWith) {
    if (find.s_.empty()) return;
    size_t pos = 0;
    while ((pos = s_.find(find.s_, pos)) != std::string::npos) {
      s_.replace(pos, find.s_.size(), replaceWith.s_);
      pos += replaceWith.s_.size();
    }
  }

  void remove(unsigned int index) {
    if (index < s_.size()) s_.erase(index);
  }

  void remove(unsigned int index, unsigned int count) {
    if (index < s_.size()) s_.erase(index, count);
  }

  void toLowerCase() {
    for (char& c : s_) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  void toUpperCase() {
    for (char& c : s_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }

  void trim() {
    size_t start = 0;
    while (start < s_.size() && std::isspace(static_cast<unsigned char>(s_[start]))) ++start;
    size_t end = s_.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s_[end - 1]))) --end;
    s_ = s_.substr(start, end - start);
  }

  long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
  float toFloat() const { return std::strtof(s_.c_str(), nullptr); }
  double toDouble() const { return std::strtod(s_.c_str(), nullptr); }

 private:
  static std::string formatUnsigned(unsigned long long value, unsigned char base) {
    if (base < 2 || base > 36) base = 10;
    char buf[66];
    char* ptr = &buf[sizeof(buf) - 1];
    *ptr = '\0';
    do {
      const unsigned long long digit = value % base;
      *--ptr = static_cast<char>(digit < 10 ? ('0' + digit) : ('a' + digit - 10));
      value /= base;
    } while (value > 0);
    return std::string(ptr);
  }

  static std::string formatSigned(long long value, unsigned char base) {
    if (base == 10 && value < 0) {
      const unsigned long long absVal = static_cast<unsigned long long>(-(value + 1)) + 1ULL;
      return "-" + formatUnsigned(absVal, 10);
    }
    return formatUnsigned(static_cast<unsigned long long>(value), base);
  }

  static std::string formatFloat(double value, unsigned char decimalPlaces) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(decimalPlaces), value);
    return std::string(buf);
  }

  std::string s_;
};

using __FlashStringHelper = char;
#ifndef F
#define F(str_literal) (str_literal)
#endif
