#pragma once

#include <cstdint>
#include <cstdlib>

#include "esp_err.h"

enum esp_reset_reason_t {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON = 1,
  ESP_RST_EXT = 2,
  ESP_RST_SW = 3,
  ESP_RST_PANIC = 4,
  ESP_RST_INT_WDT = 5,
  ESP_RST_TASK_WDT = 6,
  ESP_RST_WDT = 7,
  ESP_RST_DEEPSLEEP = 8,
  ESP_RST_BROWNOUT = 9,
  ESP_RST_SDIO = 10,
};

inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
[[noreturn]] inline void esp_restart() { std::abort(); }

class EspClass {
 public:
  void restart();
  uint32_t getFreeHeap() const;
  uint32_t getHeapSize() const;
  uint32_t getMinFreeHeap() const;
  uint32_t getMaxAllocHeap() const;
  const char* getSdkVersion() const;
  const char* getChipModel() const;
  uint8_t getChipRevision() const { return 1; }
  uint64_t getFlashChipSize() const { return 4096ULL * 1024ULL * 1024ULL; }
  uint32_t getCpuFreqMHz() const { return 168; }
};

extern EspClass ESP;
