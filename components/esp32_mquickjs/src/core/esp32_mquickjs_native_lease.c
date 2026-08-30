#include "esp32_mquickjs_native_lease.h"

#include <limits.h>

bool esp32_mquickjs_native_lease_init(
    esp32_mquickjs_native_lease_t *lease,
    esp32_mquickjs_native_pool_t *pool,
    uint16_t slot,
    uint32_t generation)
{
    if (lease == NULL || pool == NULL || slot >= pool->capacity ||
        generation == 0U) {
        return false;
    }
    lease->pool = pool;
    lease->generation = generation;
    lease->slot = slot;
    atomic_init(&lease->retain_count, 1U);
    atomic_init(&lease->close_requested, false);
    atomic_init(&lease->returned, false);
    return true;
}

bool esp32_mquickjs_native_lease_is_stale(
    const esp32_mquickjs_native_lease_t *lease, uint32_t generation)
{
    return lease == NULL || generation == 0U ||
           lease->generation != generation ||
           atomic_load_explicit(&lease->returned, memory_order_acquire);
}

bool esp32_mquickjs_native_lease_retain(
    esp32_mquickjs_native_lease_t *lease, uint32_t generation)
{
    uint32_t count;

    if (esp32_mquickjs_native_lease_is_stale(lease, generation) ||
        atomic_load_explicit(&lease->close_requested,
                             memory_order_acquire)) {
        return false;
    }
    count = atomic_load_explicit(&lease->retain_count, memory_order_acquire);
    while (count > 0U && count < UINT32_MAX) {
        if (atomic_load_explicit(&lease->close_requested,
                                 memory_order_acquire)) {
            return false;
        }
        if (atomic_compare_exchange_weak_explicit(
                &lease->retain_count, &count, count + 1U,
                memory_order_acq_rel, memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

bool esp32_mquickjs_native_lease_release(
    esp32_mquickjs_native_lease_t *lease, uint32_t generation)
{
    uint32_t count;

    if (esp32_mquickjs_native_lease_is_stale(lease, generation)) {
        return false;
    }
    count = atomic_load_explicit(&lease->retain_count, memory_order_acquire);
    while (count > 0U) {
        if (count == 1U && !atomic_load_explicit(
                &lease->close_requested, memory_order_acquire)) {
            return false;
        }
        if (atomic_compare_exchange_weak_explicit(
                &lease->retain_count, &count, count - 1U,
                memory_order_acq_rel, memory_order_acquire)) {
            if (count == 1U) {
                bool returned = atomic_exchange_explicit(
                    &lease->returned, true, memory_order_acq_rel);

                return !returned && esp32_mquickjs_native_pool_release(
                    lease->pool, lease->slot);
            }
            return true;
        }
    }
    return false;
}

bool esp32_mquickjs_native_lease_request_close(
    esp32_mquickjs_native_lease_t *lease, uint32_t generation)
{
    bool expected = false;

    if (esp32_mquickjs_native_lease_is_stale(lease, generation) ||
        !atomic_compare_exchange_strong_explicit(
            &lease->close_requested, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return false;
    }
    return esp32_mquickjs_native_lease_release(lease, generation);
}

uint32_t esp32_mquickjs_native_lease_retain_count(
    const esp32_mquickjs_native_lease_t *lease)
{
    return lease == NULL ? 0U : atomic_load_explicit(
        &lease->retain_count, memory_order_acquire);
}
