#include "esp32_mquickjs_wifi_csi_batch.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>

static void test_option_boundaries(void)
{
    assert(esp32_mquickjs_wifi_csi_batch_options_valid(16, 1, 0, 0, 16));
    assert(esp32_mquickjs_wifi_csi_batch_options_valid(
        16, 16, INT32_MAX, INT32_MAX, 16));
    assert(!esp32_mquickjs_wifi_csi_batch_options_valid(0, 1, 0, 0, 16));
    assert(!esp32_mquickjs_wifi_csi_batch_options_valid(16, 0, 0, 0, 16));
    assert(!esp32_mquickjs_wifi_csi_batch_options_valid(4, 5, 0, 0, 16));
    assert(!esp32_mquickjs_wifi_csi_batch_options_valid(17, 1, 0, 0, 16));
    assert(!esp32_mquickjs_wifi_csi_batch_options_valid(
        1, 1, (uint32_t)INT32_MAX + 1U, 0, 16));
    assert(!esp32_mquickjs_wifi_csi_batch_options_valid(
        1, 1, 0, (uint32_t)INT32_MAX + 1U, 16));
}

static void test_aggregation_decisions(void)
{
    assert(!esp32_mquickjs_wifi_csi_batch_should_return(0, 1, 8, 0, false));
    assert(esp32_mquickjs_wifi_csi_batch_should_return(1, 1, 8, 0, false));
    assert(esp32_mquickjs_wifi_csi_batch_should_return(8, 8, 8, 20, false));
    assert(!esp32_mquickjs_wifi_csi_batch_should_return(3, 4, 8, 20, false));
    assert(esp32_mquickjs_wifi_csi_batch_should_return(3, 4, 8, 20, true));
    assert(esp32_mquickjs_wifi_csi_batch_should_return(3, 4, 8, 0, false));
}

int main(void)
{
    test_option_boundaries();
    test_aggregation_decisions();
    return 0;
}
