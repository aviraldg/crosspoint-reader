#pragma once

#include "../Arduino.h"
#include "FreeRTOS.h"

using TaskFunction_t = void (*)(void*);

inline void vTaskDelay(TickType_t ticksToDelay) { delay(ticksToDelay); }

inline TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(millis()); }

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
  static int mainTask = 1;
  return &mainTask;
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
  (void)task;
  return 16384;
}

inline BaseType_t xTaskCreate(TaskFunction_t taskCode, const char* name, uint32_t stackDepth, void* parameters,
                              UBaseType_t priority, TaskHandle_t* createdTask) {
  (void)taskCode;
  (void)name;
  (void)stackDepth;
  (void)parameters;
  (void)priority;
  if (createdTask) *createdTask = nullptr;
  return pdFAIL;
}

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t taskCode, const char* name, uint32_t stackDepth,
                                          void* parameters, UBaseType_t priority, TaskHandle_t* createdTask,
                                          BaseType_t coreId) {
  (void)coreId;
  return xTaskCreate(taskCode, name, stackDepth, parameters, priority, createdTask);
}

inline void vTaskDelete(TaskHandle_t taskToDelete) { (void)taskToDelete; }
