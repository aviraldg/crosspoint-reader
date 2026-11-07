#pragma once

#include "FreeRTOS.h"

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  static int dummyMutex = 1;
  return &dummyMutex;
}

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
  static int dummyRecMutex = 1;
  return &dummyRecMutex;
}

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  static int dummyBin = 1;
  return &dummyBin;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticksToWait) {
  (void)sem;
  (void)ticksToWait;
  return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
  (void)sem;
  return pdTRUE;
}

inline void vSemaphoreDelete(SemaphoreHandle_t sem) { (void)sem; }
