#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "WString.h"

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false, const char* partitionLabel = nullptr) {
    (void)partitionLabel;
    namespace_ = name ? name : "";
    readOnly_ = readOnly;
    return true;
  }

  void end() {}

  bool clear() {
    if (readOnly_) return false;
    store()[namespace_].clear();
    return true;
  }

  bool remove(const char* key) {
    if (readOnly_ || !key) return false;
    return store()[namespace_].erase(key) > 0;
  }

  bool isKey(const char* key) const {
    if (!key) return false;
    const auto nsIt = store().find(namespace_);
    return nsIt != store().end() && nsIt->second.find(key) != nsIt->second.end();
  }

  size_t putBytes(const char* key, const void* value, size_t len) {
    if (readOnly_ || !key || (!value && len > 0)) return 0;
    const auto* bytes = static_cast<const uint8_t*>(value);
    store()[namespace_][key] = std::vector<uint8_t>(bytes, bytes + len);
    return len;
  }

  size_t getBytesLength(const char* key) const {
    if (!key) return 0;
    const auto nsIt = store().find(namespace_);
    if (nsIt == store().end()) return 0;
    const auto keyIt = nsIt->second.find(key);
    return keyIt == nsIt->second.end() ? 0 : keyIt->second.size();
  }

  size_t getBytes(const char* key, void* buf, size_t maxLen) const {
    if (!key || !buf) return 0;
    const auto nsIt = store().find(namespace_);
    if (nsIt == store().end()) return 0;
    const auto keyIt = nsIt->second.find(key);
    if (keyIt == nsIt->second.end() || keyIt->second.size() > maxLen) return 0;
    std::memcpy(buf, keyIt->second.data(), keyIt->second.size());
    return keyIt->second.size();
  }

  size_t putUChar(const char* key, uint8_t value) { return putPod(key, value); }
  uint8_t getUChar(const char* key, uint8_t defaultValue = 0) const { return getPod(key, defaultValue); }

  size_t putUShort(const char* key, uint16_t value) { return putPod(key, value); }
  uint16_t getUShort(const char* key, uint16_t defaultValue = 0) const { return getPod(key, defaultValue); }

  size_t putInt(const char* key, int32_t value) { return putPod(key, value); }
  int32_t getInt(const char* key, int32_t defaultValue = 0) const { return getPod(key, defaultValue); }

  size_t putUInt(const char* key, uint32_t value) { return putPod(key, value); }
  uint32_t getUInt(const char* key, uint32_t defaultValue = 0) const { return getPod(key, defaultValue); }

  size_t putLong64(const char* key, int64_t value) { return putPod(key, value); }
  int64_t getLong64(const char* key, int64_t defaultValue = 0) const { return getPod(key, defaultValue); }

  size_t putULong64(const char* key, uint64_t value) { return putPod(key, value); }
  uint64_t getULong64(const char* key, uint64_t defaultValue = 0) const { return getPod(key, defaultValue); }

  size_t putBool(const char* key, bool value) { return putPod<uint8_t>(key, value ? 1 : 0); }
  bool getBool(const char* key, bool defaultValue = false) const {
    return getPod<uint8_t>(key, defaultValue ? 1 : 0) != 0;
  }

  size_t putString(const char* key, const char* value) {
    if (!value) return 0;
    return putBytes(key, value, std::strlen(value));
  }
  size_t putString(const char* key, const String& value) { return putString(key, value.c_str()); }

  String getString(const char* key, const String& defaultValue = String()) const {
    if (!key) return defaultValue;
    const auto nsIt = store().find(namespace_);
    if (nsIt == store().end()) return defaultValue;
    const auto keyIt = nsIt->second.find(key);
    if (keyIt == nsIt->second.end()) return defaultValue;
    return String(reinterpret_cast<const char*>(keyIt->second.data()),
                  static_cast<unsigned int>(keyIt->second.size()));
  }

 private:
  using NamespaceMap = std::map<std::string, std::map<std::string, std::vector<uint8_t>>>;
  static NamespaceMap& store() {
    static NamespaceMap s;
    return s;
  }

  template <typename T>
  size_t putPod(const char* key, T value) {
    return putBytes(key, &value, sizeof(T));
  }

  template <typename T>
  T getPod(const char* key, T defaultValue) const {
    T out = defaultValue;
    if (getBytes(key, &out, sizeof(T)) != sizeof(T)) return defaultValue;
    return out;
  }

  std::string namespace_;
  bool readOnly_ = false;
};
