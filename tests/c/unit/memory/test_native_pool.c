#include "esp32_mquickjs_native_pool.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    esp32_mquickjs_native_pool_t pool;
    uint16_t slots[128];
    uint16_t slot;

    assert(!esp32_mquickjs_native_pool_init(&pool, 0));
    assert(!esp32_mquickjs_native_pool_init(&pool, 129));
    assert(esp32_mquickjs_native_pool_init(&pool, 128));
    for (uint16_t index = 0; index < 128; ++index) {
        assert(esp32_mquickjs_native_pool_acquire(&pool, &slots[index]));
        assert(slots[index] == index);
    }
    assert(!esp32_mquickjs_native_pool_acquire(&pool, &slot));
    assert(esp32_mquickjs_native_pool_available(&pool) == 0);
    assert(esp32_mquickjs_native_pool_release(&pool, 97));
    assert(!esp32_mquickjs_native_pool_release(&pool, 97));
    assert(esp32_mquickjs_native_pool_acquire(&pool, &slot));
    assert(slot == 97);
    assert(esp32_mquickjs_native_pool_available(&pool) == 0);
    return 0;
}
