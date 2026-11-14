#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void *;
TaskHandle_t xTaskGetCurrentTaskHandle();
int xPortGetCoreID();
int xTaskCreatePinnedToCore(void (*entry)(void *), const char *, uint32_t, void *, int, TaskHandle_t *, int);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, TickType_t);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
