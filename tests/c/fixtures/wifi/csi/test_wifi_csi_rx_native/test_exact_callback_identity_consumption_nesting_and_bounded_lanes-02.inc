#pragma once
#include <stdint.h>
typedef void *TaskHandle_t;extern uintptr_t task_id;
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)task_id;}
