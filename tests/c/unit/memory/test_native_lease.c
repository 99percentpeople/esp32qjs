#include "esp32_mquickjs_native_lease.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    esp32_mquickjs_native_pool_t pool;
    esp32_mquickjs_native_lease_t first;
    esp32_mquickjs_native_lease_t second;
    uint16_t slot;

    assert(esp32_mquickjs_native_pool_init(&pool, 1));
    assert(esp32_mquickjs_native_pool_acquire(&pool, &slot));
    assert(esp32_mquickjs_native_lease_init(&first, &pool, slot, 7));
    assert(esp32_mquickjs_native_lease_retain(&first, 7));
    assert(esp32_mquickjs_native_lease_retain_count(&first) == 2);
    assert(esp32_mquickjs_native_lease_request_close(&first, 7));
    assert(esp32_mquickjs_native_lease_retain_count(&first) == 1);
    assert(esp32_mquickjs_native_pool_available(&pool) == 0);
    assert(!esp32_mquickjs_native_lease_retain(&first, 7));
    assert(!esp32_mquickjs_native_lease_request_close(&first, 7));
    assert(esp32_mquickjs_native_lease_release(&first, 7));
    assert(esp32_mquickjs_native_pool_available(&pool) == 1);
    assert(esp32_mquickjs_native_lease_is_stale(&first, 7));
    assert(!esp32_mquickjs_native_lease_release(&first, 7));

    assert(esp32_mquickjs_native_pool_acquire(&pool, &slot));
    assert(esp32_mquickjs_native_lease_init(&second, &pool, slot, 8));
    assert(esp32_mquickjs_native_lease_is_stale(&second, 7));
    assert(!esp32_mquickjs_native_lease_retain(&second, 7));
    assert(esp32_mquickjs_native_lease_request_close(&second, 8));
    assert(esp32_mquickjs_native_pool_available(&pool) == 1);
    return 0;
}
