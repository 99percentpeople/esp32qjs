#include "esp32_mquickjs_future_timeout.h"

#include <stdint.h>

uint32_t esp32_mquickjs_future_elapsed_timeout_ms(uint64_t submitted_us,
                                                  uint64_t deadline_us)
{
    uint64_t duration_ms;

    if (deadline_us <= submitted_us) {
        return 0;
    }
    duration_ms = (deadline_us - submitted_us) / 1000ULL;
    if (duration_ms > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)duration_ms;
}
