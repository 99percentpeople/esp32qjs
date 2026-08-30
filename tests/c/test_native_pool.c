#include "esp32_mquickjs_native_pool.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    esp32_mquickjs_native_pool_t pool;
    uint16_t slots[64];
    uint16_t slot;

    assert(!esp32_mquickjs_native_pool_init(&pool, 0));
    assert(!esp32_mquickjs_native_pool_init(&pool, 65));
    assert(esp32_mquickjs_native_pool_init(&pool, 64));
    for (uint16_t index = 0; index < 64; ++index) {
        assert(esp32_mquickjs_native_pool_acquire(&pool, &slots[index]));
        assert(slots[index] == index);
    }
    assert(!esp32_mquickjs_native_pool_acquire(&pool, &slot));
    assert(esp32_mquickjs_native_pool_available(&pool) == 0);
    assert(esp32_mquickjs_native_pool_release(&pool, 33));
    assert(!esp32_mquickjs_native_pool_release(&pool, 33));
    assert(esp32_mquickjs_native_pool_acquire(&pool, &slot));
    assert(slot == 33);
    return 0;
}
