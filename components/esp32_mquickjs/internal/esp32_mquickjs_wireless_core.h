#ifndef ESP32_MQUICKJS_WIRELESS_CORE_H
#define ESP32_MQUICKJS_WIRELESS_CORE_H

#include "esp32_mquickjs_native_pool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP32_MQUICKJS_WIRELESS_ADDRESS_BYTES 6U

typedef enum {
    ESP32_MQUICKJS_WIRELESS_TX_READY = 0,
    ESP32_MQUICKJS_WIRELESS_TX_TIMED_OUT,
    ESP32_MQUICKJS_WIRELESS_TX_RECOVERING,
    ESP32_MQUICKJS_WIRELESS_TX_FAILED,
} esp32_mquickjs_wireless_tx_state_t;

typedef enum {
    ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_IDLE = 0,
    ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_ACTIVE,
    ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_CANCELLING,
} esp32_mquickjs_wireless_native_operation_state_t;

typedef struct {
    _Atomic uint8_t state;
} esp32_mquickjs_wireless_native_operation_t;

typedef struct {
    bool occupied;
    uint32_t generation;
    uint16_t index;
} esp32_mquickjs_wireless_critical_slot_t;

typedef struct {
    bool claimed;
    uint32_t lease_identity;
    uint32_t client;
    uint8_t primary_channel;
    uint8_t secondary_channel;
    uint32_t generation;
} esp32_mquickjs_wireless_fixed_channel_owner_t;

typedef enum {
    ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_INVALID = 0,
    ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_CONFLICT,
    ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_ACQUIRE,
    ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_CHANGE,
    ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_IDEMPOTENT,
} esp32_mquickjs_wireless_fixed_channel_result_t;

typedef bool (*esp32_mquickjs_wireless_event_send_from_isr_fn)(
    void *destination, const void *event, int *task_woken);
typedef bool (*esp32_mquickjs_wireless_event_try_send_from_callback_fn)(
    void *destination, const void *event);

bool esp32_mquickjs_wireless_parse_address(
    const char *text,
    uint8_t output[ESP32_MQUICKJS_WIRELESS_ADDRESS_BYTES]);
void esp32_mquickjs_wireless_format_address(
    const uint8_t address[ESP32_MQUICKJS_WIRELESS_ADDRESS_BYTES],
    char output[18]);

bool esp32_mquickjs_wireless_key_length_valid(size_t length,
                                               size_t required_length);
void esp32_mquickjs_wireless_secure_zero(void *data, size_t length);

bool esp32_mquickjs_wireless_pooled_event_publish_from_isr(
    esp32_mquickjs_native_pool_t *pool,
    uint16_t pool_index,
    void *destination,
    const void *event,
    esp32_mquickjs_wireless_event_send_from_isr_fn send,
    int *task_woken);
bool esp32_mquickjs_wireless_pooled_event_publish_from_callback(
    esp32_mquickjs_native_pool_t *pool,
    uint16_t pool_index,
    void *destination,
    const void *event,
    esp32_mquickjs_wireless_event_try_send_from_callback_fn try_send);

bool esp32_mquickjs_wireless_tx_timeout(
    esp32_mquickjs_wireless_tx_state_t *state);
bool esp32_mquickjs_wireless_tx_begin_recovery(
    esp32_mquickjs_wireless_tx_state_t *state);
bool esp32_mquickjs_wireless_tx_finish_recovery(
    esp32_mquickjs_wireless_tx_state_t *state, bool success);
bool esp32_mquickjs_wireless_tx_retry_recovery(
    esp32_mquickjs_wireless_tx_state_t *state);

void esp32_mquickjs_wireless_native_operation_init(
    esp32_mquickjs_wireless_native_operation_t *operation);
bool esp32_mquickjs_wireless_native_operation_begin(
    esp32_mquickjs_wireless_native_operation_t *operation);
bool esp32_mquickjs_wireless_native_operation_request_cancel(
    esp32_mquickjs_wireless_native_operation_t *operation);
bool esp32_mquickjs_wireless_native_operation_complete(
    esp32_mquickjs_wireless_native_operation_t *operation);
bool esp32_mquickjs_wireless_native_operation_is_quiescent(
    const esp32_mquickjs_wireless_native_operation_t *operation);
bool esp32_mquickjs_wireless_close_can_release(
    uint32_t callbacks_active,
    bool native_operations_quiescent);

bool esp32_mquickjs_wireless_uuid_valid(const char *text);
bool esp32_mquickjs_wireless_local_value_write(
    uint8_t *value, uint16_t max_length, uint16_t *value_length,
    uint16_t offset, const uint8_t *data, uint16_t data_length);
bool esp32_mquickjs_wireless_gatt_descriptor_end(
    uint16_t characteristic_index,
    uint16_t service_first_characteristic,
    uint16_t service_characteristic_count,
    uint16_t service_end_handle,
    uint16_t next_declaration_handle,
    uint16_t *out_end_handle);
bool esp32_mquickjs_wireless_generation_matches(
    uint32_t active_generation, uint32_t event_generation, bool closing);

bool esp32_mquickjs_wireless_critical_reserve(
    esp32_mquickjs_wireless_critical_slot_t *slot,
    uint32_t generation, uint16_t index);
bool esp32_mquickjs_wireless_critical_take(
    esp32_mquickjs_wireless_critical_slot_t *slot,
    uint32_t generation, uint16_t *out_index);

esp32_mquickjs_wireless_fixed_channel_result_t
esp32_mquickjs_wireless_fixed_channel_check(
    const esp32_mquickjs_wireless_fixed_channel_owner_t *owner,
    uint32_t lease_identity, uint32_t client,
    uint8_t primary_channel, uint8_t secondary_channel);
bool esp32_mquickjs_wireless_fixed_channel_claim(
    esp32_mquickjs_wireless_fixed_channel_owner_t *owner,
    uint32_t lease_identity, uint32_t client,
    uint8_t primary_channel, uint8_t secondary_channel);
bool esp32_mquickjs_wireless_fixed_channel_release(
    esp32_mquickjs_wireless_fixed_channel_owner_t *owner,
    uint32_t lease_identity, uint32_t client);

#ifdef __cplusplus
}
#endif

#endif
