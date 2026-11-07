#pragma once

#include "esp_err.h"

inline esp_err_t esp_task_wdt_status(void* handle) {
  (void)handle;
  return ESP_ERR_NOT_FOUND;
}

inline esp_err_t esp_task_wdt_reset() { return ESP_OK; }
