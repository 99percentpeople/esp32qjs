#include "esp32_mquickjs_native_pool.h"

bool esp32_mquickjs_native_pool_init(
    esp32_mquickjs_native_pool_t *pool, uint32_t capacity)
{
    uint32_t word_index;

    if (pool == NULL || capacity == 0U ||
        capacity > ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY) {
        return false;
    }
    for (word_index = 0;
         word_index < ESP32_MQUICKJS_NATIVE_POOL_WORD_COUNT;
         ++word_index) {
        uint32_t first_slot = word_index * 32U;
        uint32_t remaining = capacity > first_slot
            ? capacity - first_slot
            : 0U;
        uint32_t bits = remaining >= 32U
            ? UINT32_MAX
            : remaining == 0U
                ? 0U
                : (UINT32_C(1) << remaining) - 1U;

        atomic_init(&pool->free_bits[word_index], bits);
    }
    pool->capacity = (uint8_t)capacity;
    return true;
}

bool esp32_mquickjs_native_pool_acquire(
    esp32_mquickjs_native_pool_t *pool, uint16_t *out_index)
{
    uint32_t word_index;

    if (pool == NULL || out_index == NULL || pool->capacity == 0U) {
        return false;
    }
    for (word_index = 0;
         word_index < ESP32_MQUICKJS_NATIVE_POOL_WORD_COUNT;
         ++word_index) {
        uint32_t current = atomic_load_explicit(
            &pool->free_bits[word_index], memory_order_acquire);

        while (current != 0U) {
            uint32_t bit = 0U;
            uint32_t probe = current;
            uint32_t updated;

            while ((probe & 1U) == 0U) {
                probe >>= 1U;
                bit++;
            }
            updated = current & ~(UINT32_C(1) << bit);
            if (atomic_compare_exchange_weak_explicit(
                    &pool->free_bits[word_index], &current, updated,
                    memory_order_acq_rel, memory_order_acquire)) {
                *out_index = (uint16_t)(word_index * 32U + bit);
                return *out_index < pool->capacity;
            }
        }
    }
    return false;
}

bool esp32_mquickjs_native_pool_release(
    esp32_mquickjs_native_pool_t *pool, uint16_t index)
{
    uint32_t word_index;
    uint32_t mask;
    uint32_t previous;

    if (pool == NULL || index >= pool->capacity) {
        return false;
    }
    word_index = index / 32U;
    mask = UINT32_C(1) << (index % 32U);
    previous = atomic_fetch_or_explicit(&pool->free_bits[word_index], mask,
                                        memory_order_release);
    return (previous & mask) == 0U;
}

uint32_t esp32_mquickjs_native_pool_available(
    const esp32_mquickjs_native_pool_t *pool)
{
    uint32_t available = 0U;
    uint32_t word_index;

    if (pool == NULL) {
        return 0U;
    }
    for (word_index = 0;
         word_index < ESP32_MQUICKJS_NATIVE_POOL_WORD_COUNT;
         ++word_index) {
        uint32_t bits = atomic_load_explicit(&pool->free_bits[word_index],
                                             memory_order_acquire);

        while (bits != 0U) {
            available += bits & 1U;
            bits >>= 1U;
        }
    }
    return available;
}
