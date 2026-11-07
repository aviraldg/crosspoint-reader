#pragma once

#include <cstdint>

#include "esp_err.h"

enum esp_sleep_wakeup_cause_t {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_ALL = 1,
  ESP_SLEEP_WAKEUP_EXT0 = 2,
  ESP_SLEEP_WAKEUP_EXT1 = 3,
  ESP_SLEEP_WAKEUP_TIMER = 4,
  ESP_SLEEP_WAKEUP_TOUCHPAD = 5,
  ESP_SLEEP_WAKEUP_ULP = 6,
  ESP_SLEEP_WAKEUP_GPIO = 7,
};

inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
inline void esp_deep_sleep_start() {}
inline esp_err_t esp_light_sleep_start() { return ESP_OK; }
inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us) {
  (void)time_in_us;
  return ESP_OK;
}
