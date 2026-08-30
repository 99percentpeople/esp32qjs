#ifndef ESP32_MQUICKJS_NATIVE_LEASE_H
#define ESP32_MQUICKJS_NATIVE_LEASE_H

#include "esp32_mquickjs_native_pool.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp32_mquickjs_native_pool_t *pool;
    uint32_t generation;
    uint16_t slot;
    _Atomic uint32_t retain_count;
    _Atomic bool close_requested;
    _Atomic bool returned;
} esp32_mquickjs_native_lease_t;

bool esp32_mquickjs_native_lease_init(
    esp32_mquickjs_native_lease_t *lease,
    esp32_mquickjs_native_pool_t *pool,
    uint16_t slot,
    uint32_t generation);
bool esp32_mquickjs_native_lease_retain(
    esp32_mquickjs_native_lease_t *lease, uint32_t generation);
bool esp32_mquickjs_native_lease_release(
    esp32_mquickjs_native_lease_t *lease, uint32_t generation);
bool esp32_mquickjs_native_lease_request_close(
    esp32_mquickjs_native_lease_t *lease, uint32_t generation);
bool esp32_mquickjs_native_lease_is_stale(
    const esp32_mquickjs_native_lease_t *lease, uint32_t generation);
uint32_t esp32_mquickjs_native_lease_retain_count(
    const esp32_mquickjs_native_lease_t *lease);

#ifdef __cplusplus
}
#endif

#endif
