#pragma once

#include <cstdint>

#include "esp_err.h"

enum esp_mac_type_t {
  ESP_MAC_WIFI_STA = 0,
  ESP_MAC_WIFI_SOFTAP = 1,
  ESP_MAC_BT = 2,
  ESP_MAC_ETH = 3,
  ESP_MAC_EFUSE_FACTORY = 4,
  ESP_MAC_BASE = 5,
};

inline esp_err_t esp_efuse_mac_get_default(uint8_t* mac) {
  if (!mac) return ESP_ERR_INVALID_ARG;
  mac[0] = 0x02;
  mac[1] = 0x00;
  mac[2] = 0x50;
  mac[3] = 0x44;
  mac[4] = 0x00;
  mac[5] = 0x01;
  return ESP_OK;
}

inline esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type) {
  (void)type;
  return esp_efuse_mac_get_default(mac);
}
