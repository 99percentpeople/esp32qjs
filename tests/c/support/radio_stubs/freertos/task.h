#pragma once
#include <sched.h>
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
static _Thread_local TickType_t radio_test_ticks;
typedef void *TaskHandle_t;
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &radio_test_ticks; }
void radio_test_delay(void);
static inline TickType_t xTaskGetTickCount(void) { return radio_test_ticks; }
static inline void vTaskDelay(int ticks) { radio_test_ticks+=ticks;radio_test_delay();sched_yield(); }
