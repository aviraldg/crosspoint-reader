#pragma once

#include <cstdint>

#include "esp_err.h"

enum wifi_ps_type_t {
  WIFI_PS_NONE = 0,
  WIFI_PS_MIN_MODEM = 1,
  WIFI_PS_MAX_MODEM = 2,
};

inline esp_err_t esp_wifi_set_max_tx_power(int8_t power) {
  (void)power;
  return ESP_OK;
}

inline esp_err_t esp_wifi_get_max_tx_power(int8_t* power) {
  if (power) *power = 78;
  return ESP_OK;
}

inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) {
  (void)type;
  return ESP_OK;
}

inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* type) {
  if (type) *type = WIFI_PS_NONE;
  return ESP_OK;
}
