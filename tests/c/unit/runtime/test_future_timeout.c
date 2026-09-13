#include "esp32_mquickjs_future_timeout.h"

#include <assert.h>
#include <stdint.h>

static void test_expired_inherited_deadline_saturates_to_zero(void)
{
    assert(esp32_mquickjs_future_elapsed_timeout_ms(5000, 4000) == 0);
    assert(esp32_mquickjs_future_elapsed_timeout_ms(5000, 5000) == 0);
}

static void test_duration_is_reported_in_whole_milliseconds(void)
{
    assert(esp32_mquickjs_future_elapsed_timeout_ms(5000, 5999) == 0);
    assert(esp32_mquickjs_future_elapsed_timeout_ms(5000, 6000) == 1);
    assert(esp32_mquickjs_future_elapsed_timeout_ms(5000, 7500) == 2);
}

static void test_large_duration_saturates_to_uint32_max(void)
{
    uint64_t maximum_ms = UINT32_MAX;

    assert(esp32_mquickjs_future_elapsed_timeout_ms(
               0, maximum_ms * 1000ULL) == UINT32_MAX);
    assert(esp32_mquickjs_future_elapsed_timeout_ms(
               0, (maximum_ms + 1ULL) * 1000ULL) == UINT32_MAX);
    assert(esp32_mquickjs_future_elapsed_timeout_ms(0, UINT64_MAX) == UINT32_MAX);
}

int main(void)
{
    test_expired_inherited_deadline_saturates_to_zero();
    test_duration_is_reported_in_whole_milliseconds();
    test_large_duration_saturates_to_uint32_max();
    return 0;
}
