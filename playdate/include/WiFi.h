#pragma once

#include <cstdint>
#include <cstdio>

#include "WString.h"

enum wl_status_t {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6
};

enum wifi_mode_t {
  WIFI_OFF = 0,
  WIFI_MODE_NULL = 0,
  WIFI_STA = 1,
  WIFI_AP = 2,
  WIFI_AP_STA = 3
};

enum wifi_auth_mode_t {
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_WPA2_ENTERPRISE
};

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

class IPAddress {
 public:
  constexpr IPAddress() : octets_{127, 0, 0, 1} {}
  constexpr IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : octets_{a, b, c, d} {}
  explicit constexpr IPAddress(uint32_t addr)
      : octets_{static_cast<uint8_t>(addr & 0xFF), static_cast<uint8_t>((addr >> 8) & 0xFF),
                static_cast<uint8_t>((addr >> 16) & 0xFF), static_cast<uint8_t>((addr >> 24) & 0xFF)} {}

  uint8_t operator[](int index) const { return (index >= 0 && index < 4) ? octets_[index] : 0; }
  uint8_t& operator[](int index) { return octets_[index]; }

  constexpr bool operator==(const IPAddress& other) const {
    return octets_[0] == other.octets_[0] && octets_[1] == other.octets_[1] && octets_[2] == other.octets_[2] &&
           octets_[3] == other.octets_[3];
  }
  constexpr bool operator!=(const IPAddress& other) const { return !(*this == other); }

  String toString() const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", octets_[0], octets_[1], octets_[2], octets_[3]);
    return String(buf);
  }

 private:
  uint8_t octets_[4];
};

class WiFiClass {
 public:
  bool mode(wifi_mode_t m) {
    mode_ = m;
    if (m == WIFI_OFF) {
      status_ = WL_DISCONNECTED;
    }
    return true;
  }
  wifi_mode_t getMode() const { return mode_; }

  wl_status_t begin(const char* ssid = nullptr, const char* passphrase = nullptr) {
    if (ssid) ssid_ = ssid;
    (void)passphrase;
    mode_ = WIFI_STA;
    status_ = WL_CONNECTED;
    return status_;
  }

  bool disconnect(bool wifioff = false, bool eraseap = false) {
    (void)eraseap;
    if (wifioff) mode_ = WIFI_OFF;
    status_ = WL_DISCONNECTED;
    return true;
  }

  wl_status_t status() const { return status_; }
  String SSID() const { return ssid_.isEmpty() ? String("Playdate Net") : ssid_; }
  IPAddress localIP() const { return IPAddress(127, 0, 0, 1); }
  String macAddress() const { return String("02:00:50:44:00:01"); }
  int32_t RSSI() const { return -50; }
  bool setSleep(bool enabled) {
    (void)enabled;
    return true;
  }
  bool setHostname(const char* hostname) {
    (void)hostname;
    return true;
  }

  int16_t scanNetworks(bool async = false, bool showHidden = false) {
    (void)async;
    (void)showHidden;
    return 1;
  }
  int16_t scanComplete() const { return 1; }
  void scanDelete() {}
  String SSID(uint8_t i) const {
    (void)i;
    return String("Playdate Host Wi-Fi");
  }
  int32_t RSSI(uint8_t i) const {
    (void)i;
    return -45;
  }
  wifi_auth_mode_t encryptionType(uint8_t i) const {
    (void)i;
    return WIFI_AUTH_OPEN;
  }

  bool softAP(const char* ssid, const char* passphrase = nullptr, int channel = 1, int ssidHidden = 0,
              int maxConnection = 4) {
    (void)ssid;
    (void)passphrase;
    (void)channel;
    (void)ssidHidden;
    (void)maxConnection;
    return false;
  }
  bool softAPDisconnect(bool wifioff = false) {
    (void)wifioff;
    return true;
  }
  IPAddress softAPIP() const { return IPAddress(0, 0, 0, 0); }
  uint8_t softAPgetStationNum() const { return 0; }

 private:
  wifi_mode_t mode_ = WIFI_STA;
  wl_status_t status_ = WL_CONNECTED;
  String ssid_ = "Playdate Host Wi-Fi";
};

inline WiFiClass WiFi;
