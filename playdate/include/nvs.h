#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

using nvs_handle_t = uint32_t;

enum nvs_open_mode_t {
  NVS_READONLY = 0,
  NVS_READWRITE = 1,
};

#define ESP_ERR_NVS_NOT_FOUND 0x1102

inline esp_err_t nvs_open(const char* name, nvs_open_mode_t open_mode, nvs_handle_t* out_handle) {
  (void)name;
  (void)open_mode;
  if (out_handle) *out_handle = 1;
  return ESP_OK;
}

inline void nvs_close(nvs_handle_t handle) { (void)handle; }

inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length) {
  (void)handle;
  (void)key;
  (void)out_value;
  (void)length;
  return ESP_ERR_NVS_NOT_FOUND;
}

inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) {
  (void)handle;
  (void)key;
  (void)value;
  (void)length;
  return ESP_OK;
}

inline esp_err_t nvs_commit(nvs_handle_t handle) {
  (void)handle;
  return ESP_OK;
}
