#pragma once

#include <cstddef>
#include <cstdint>

using std::ptrdiff_t;
using std::size_t;

using TickType_t = uint32_t;
using BaseType_t = int32_t;
using UBaseType_t = uint32_t;
using TaskHandle_t = void*;
using QueueHandle_t = void*;
using SemaphoreHandle_t = void*;

struct StaticTask_t {
  uint8_t dummy[64];
};

struct StaticSemaphore_t {
  uint8_t dummy[16];
};

#ifndef pdTRUE
#define pdTRUE 1
#endif

#ifndef pdFALSE
#define pdFALSE 0
#endif

#ifndef pdPASS
#define pdPASS pdTRUE
#endif

#ifndef pdFAIL
#define pdFAIL pdFALSE
#endif

#ifndef portMAX_DELAY
#define portMAX_DELAY 0xFFFFFFFFUL
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#endif

struct portMUX_TYPE {
  int dummy = 0;
};

#ifndef portMUX_INITIALIZER_UNLOCKED
#define portMUX_INITIALIZER_UNLOCKED {0}
#endif

#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL(mux) ((void)(mux))
#endif

#ifndef portEXIT_CRITICAL
#define portEXIT_CRITICAL(mux) ((void)(mux))
#endif

#ifndef taskENTER_CRITICAL
#define taskENTER_CRITICAL(mux) portENTER_CRITICAL(mux)
#endif

#ifndef taskEXIT_CRITICAL
#define taskEXIT_CRITICAL(mux) portEXIT_CRITICAL(mux)
#endif
