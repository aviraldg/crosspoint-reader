#pragma once

#include <cstdint>

#include "../esp_err.h"

enum gpio_num_t : int {
  GPIO_NUM_NC = -1,
  GPIO_NUM_0 = 0,
  GPIO_NUM_1 = 1,
  GPIO_NUM_2 = 2,
  GPIO_NUM_3 = 3,
  GPIO_NUM_4 = 4,
  GPIO_NUM_5 = 5,
  GPIO_NUM_6 = 6,
  GPIO_NUM_7 = 7,
  GPIO_NUM_8 = 8,
  GPIO_NUM_9 = 9,
  GPIO_NUM_10 = 10,
  GPIO_NUM_11 = 11,
  GPIO_NUM_12 = 12,
  GPIO_NUM_13 = 13,
  GPIO_NUM_20 = 20,
  GPIO_NUM_21 = 21,
};

enum gpio_int_type_t {
  GPIO_INTR_DISABLE = 0,
  GPIO_INTR_POSEDGE = 1,
  GPIO_INTR_NEGEDGE = 2,
  GPIO_INTR_ANYEDGE = 3,
  GPIO_INTR_LOW_LEVEL = 4,
  GPIO_INTR_HIGH_LEVEL = 5,
};

inline esp_err_t gpio_deep_sleep_hold_dis() { return ESP_OK; }
inline esp_err_t gpio_hold_dis(gpio_num_t gpio_num) {
  (void)gpio_num;
  return ESP_OK;
}
inline esp_err_t gpio_hold_en(gpio_num_t gpio_num) {
  (void)gpio_num;
  return ESP_OK;
}

