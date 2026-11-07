#pragma once

#include <cstdint>

enum sntp_sync_mode_t {
  SNTP_SYNC_MODE_IMMED = 0,
  SNTP_SYNC_MODE_SMOOTH = 1,
};

enum sntp_sync_status_t {
  SNTP_SYNC_STATUS_RESET = 0,
  SNTP_SYNC_STATUS_COMPLETED = 1,
  SNTP_SYNC_STATUS_IN_PROGRESS = 2,
};

#define SNTP_OPMODE_POLL 0

inline void esp_sntp_setoperatingmode(uint8_t operating_mode) { (void)operating_mode; }
inline void esp_sntp_setservername(uint8_t idx, const char* server) {
  (void)idx;
  (void)server;
}
inline void esp_sntp_init() {}
inline void esp_sntp_stop() {}
inline bool esp_sntp_enabled() { return false; }
inline sntp_sync_status_t sntp_get_sync_status() { return SNTP_SYNC_STATUS_COMPLETED; }
