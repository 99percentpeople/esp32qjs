#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ESP32_MQUICKJS_PERIPHERAL_I2S_PORT,
    ESP32_MQUICKJS_PERIPHERAL_CAMERA,
    ESP32_MQUICKJS_PERIPHERAL_I2C_PORT,
    ESP32_MQUICKJS_PERIPHERAL_LEDC_TIMER,
    ESP32_MQUICKJS_PERIPHERAL_LEDC_CHANNEL,
    ESP32_MQUICKJS_PERIPHERAL_KIND_COUNT,
} esp32_mquickjs_peripheral_kind_t;

typedef enum {
    ESP32_MQUICKJS_PERIPHERAL_OWNER_NONE,
    ESP32_MQUICKJS_PERIPHERAL_OWNER_I2S,
    ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA,
    ESP32_MQUICKJS_PERIPHERAL_OWNER_I2C,
    ESP32_MQUICKJS_PERIPHERAL_OWNER_LEDC,
} esp32_mquickjs_peripheral_owner_t;

typedef struct {
    esp32_mquickjs_peripheral_kind_t kind;
    esp32_mquickjs_peripheral_owner_t owner;
    int8_t index;
    uint32_t generation;
    bool held;
} esp32_mquickjs_peripheral_lease_t;

void esp32_mquickjs_peripheral_leases_reset(void);
bool esp32_mquickjs_peripheral_lease_acquire(
    esp32_mquickjs_peripheral_kind_t kind,
    int index,
    esp32_mquickjs_peripheral_owner_t owner,
    esp32_mquickjs_peripheral_lease_t *out_lease);
bool esp32_mquickjs_peripheral_lease_acquire_any(
    esp32_mquickjs_peripheral_kind_t kind,
    int count,
    esp32_mquickjs_peripheral_owner_t owner,
    esp32_mquickjs_peripheral_lease_t *out_lease);
void esp32_mquickjs_peripheral_lease_release(
    esp32_mquickjs_peripheral_lease_t *lease);
bool esp32_mquickjs_peripheral_lease_is_held(
    const esp32_mquickjs_peripheral_lease_t *lease);
