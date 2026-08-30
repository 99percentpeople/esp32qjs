#ifndef ESP32_MQUICKJS_NATIVE_POOL_H
#define ESP32_MQUICKJS_NATIVE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY 128U
#define ESP32_MQUICKJS_NATIVE_POOL_WORD_COUNT \
    (ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY / 32U)

typedef struct {
    _Atomic uint32_t free_bits[ESP32_MQUICKJS_NATIVE_POOL_WORD_COUNT];
    uint8_t capacity;
} esp32_mquickjs_native_pool_t;

bool esp32_mquickjs_native_pool_init(
    esp32_mquickjs_native_pool_t *pool, uint32_t capacity);
bool esp32_mquickjs_native_pool_acquire(
    esp32_mquickjs_native_pool_t *pool, uint16_t *out_index);
bool esp32_mquickjs_native_pool_release(
    esp32_mquickjs_native_pool_t *pool, uint16_t index);
uint32_t esp32_mquickjs_native_pool_available(
    const esp32_mquickjs_native_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif
