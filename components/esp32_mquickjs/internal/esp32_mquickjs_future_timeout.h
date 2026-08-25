#pragma once

#include <stdint.h>

uint32_t esp32_mquickjs_future_elapsed_timeout_ms(uint64_t submitted_us,
                                                  uint64_t deadline_us);
