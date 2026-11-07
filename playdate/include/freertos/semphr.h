#pragma once

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

struct PlaydateSemaphoreState {
  int takenCount = 0;
};

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  static PlaydateSemaphoreState dummyMutex{};
  return &dummyMutex;
}

inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* pxMutexBuffer) {
  static PlaydateSemaphoreState dummyStaticMutex{};
  return pxMutexBuffer ? static_cast<SemaphoreHandle_t>(pxMutexBuffer) : &dummyStaticMutex;
}

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
  static PlaydateSemaphoreState dummyRecMutex{};
  return &dummyRecMutex;
}

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  static PlaydateSemaphoreState dummyBin{};
  return &dummyBin;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticksToWait) {
  (void)ticksToWait;
  if (sem) {
    static_cast<PlaydateSemaphoreState*>(sem)->takenCount++;
  }
  return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
  if (sem) {
    auto* state = static_cast<PlaydateSemaphoreState*>(sem);
    if (state->takenCount > 0) state->takenCount--;
  }
  return pdTRUE;
}

inline BaseType_t xQueuePeek(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait) {
  (void)pvBuffer;
  (void)xTicksToWait;
  if (xQueue && static_cast<PlaydateSemaphoreState*>(xQueue)->takenCount > 0) {
    return pdFALSE;
  }
  return pdTRUE;
}

inline TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t xMutex) {
  if (xMutex && static_cast<PlaydateSemaphoreState*>(xMutex)->takenCount > 0) {
    return xTaskGetCurrentTaskHandle();
  }
  return nullptr;
}

inline void vSemaphoreDelete(SemaphoreHandle_t sem) { (void)sem; }
