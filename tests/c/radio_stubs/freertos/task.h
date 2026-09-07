#pragma once
#include <sched.h>
static inline void vTaskDelay(int ticks) { (void)ticks; sched_yield(); }
