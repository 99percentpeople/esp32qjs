#include "esp32_mquickjs_wireless_core.h"

#include <string.h>
#include <stdio.h>

static int wireless_hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool esp32_mquickjs_wireless_parse_address(const char *text,
                                            uint8_t output[6])
{
    size_t index;
    if (text == NULL || output == NULL || strlen(text) != 17U) return false;
    for (index = 0; index < 6U; ++index) {
        int high = wireless_hex_nibble(text[index * 3U]);
        int low = wireless_hex_nibble(text[index * 3U + 1U]);
        if (high < 0 || low < 0 ||
            (index < 5U && text[index * 3U + 2U] != ':')) return false;
        output[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

void esp32_mquickjs_wireless_format_address(const uint8_t address[6],
                                             char output[18])
{
    if (address == NULL || output == NULL) return;
    (void)snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
                   address[0], address[1], address[2], address[3],
                   address[4], address[5]);
}

bool esp32_mquickjs_wireless_key_length_valid(size_t length,
                                               size_t required_length)
{
    return required_length > 0U && length == required_length;
}

void esp32_mquickjs_wireless_secure_zero(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (bytes != NULL && length-- > 0U) *bytes++ = 0U;
}

bool esp32_mquickjs_wireless_pooled_event_publish_from_isr(
    esp32_mquickjs_native_pool_t *pool,
    uint16_t pool_index,
    void *destination,
    const void *event,
    esp32_mquickjs_wireless_event_send_from_isr_fn send,
    int *task_woken)
{
    if (pool == NULL || pool_index >= pool->capacity ||
        destination == NULL || event == NULL || send == NULL ||
        !send(destination, event, task_woken)) {
        if (pool != NULL) {
            (void)esp32_mquickjs_native_pool_release(pool, pool_index);
        }
        return false;
    }
    return true;
}

bool esp32_mquickjs_wireless_pooled_event_publish_from_callback(
    esp32_mquickjs_native_pool_t *pool,
    uint16_t pool_index,
    void *destination,
    const void *event,
    esp32_mquickjs_wireless_event_try_send_from_callback_fn try_send)
{
    if (pool == NULL || pool_index >= pool->capacity ||
        destination == NULL || event == NULL || try_send == NULL ||
        !try_send(destination, event)) {
        if (pool != NULL) {
            (void)esp32_mquickjs_native_pool_release(pool, pool_index);
        }
        return false;
    }
    return true;
}

bool esp32_mquickjs_wireless_tx_timeout(
    esp32_mquickjs_wireless_tx_state_t *state)
{
    if (state == NULL || *state != ESP32_MQUICKJS_WIRELESS_TX_READY)
        return false;
    *state = ESP32_MQUICKJS_WIRELESS_TX_TIMED_OUT;
    return true;
}

bool esp32_mquickjs_wireless_tx_begin_recovery(
    esp32_mquickjs_wireless_tx_state_t *state)
{
    if (state == NULL || *state != ESP32_MQUICKJS_WIRELESS_TX_TIMED_OUT)
        return false;
    *state = ESP32_MQUICKJS_WIRELESS_TX_RECOVERING;
    return true;
}

bool esp32_mquickjs_wireless_tx_finish_recovery(
    esp32_mquickjs_wireless_tx_state_t *state, bool success)
{
    if (state == NULL || *state != ESP32_MQUICKJS_WIRELESS_TX_RECOVERING)
        return false;
    *state = success ? ESP32_MQUICKJS_WIRELESS_TX_READY
                     : ESP32_MQUICKJS_WIRELESS_TX_FAILED;
    return true;
}

bool esp32_mquickjs_wireless_tx_retry_recovery(
    esp32_mquickjs_wireless_tx_state_t *state)
{
    if (state == NULL || *state != ESP32_MQUICKJS_WIRELESS_TX_FAILED)
        return false;
    *state = ESP32_MQUICKJS_WIRELESS_TX_RECOVERING;
    return true;
}

void esp32_mquickjs_wireless_native_operation_init(
    esp32_mquickjs_wireless_native_operation_t *operation)
{
    if (operation == NULL) return;
    atomic_init(&operation->state,
                ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_IDLE);
}

bool esp32_mquickjs_wireless_native_operation_begin(
    esp32_mquickjs_wireless_native_operation_t *operation)
{
    uint8_t expected = ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_IDLE;
    return operation != NULL && atomic_compare_exchange_strong_explicit(
        &operation->state, &expected,
        ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_ACTIVE,
        memory_order_acq_rel, memory_order_acquire);
}

bool esp32_mquickjs_wireless_native_operation_request_cancel(
    esp32_mquickjs_wireless_native_operation_t *operation)
{
    uint8_t expected = ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_ACTIVE;
    return operation != NULL && atomic_compare_exchange_strong_explicit(
        &operation->state, &expected,
        ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_CANCELLING,
        memory_order_acq_rel, memory_order_acquire);
}

bool esp32_mquickjs_wireless_native_operation_complete(
    esp32_mquickjs_wireless_native_operation_t *operation)
{
    uint8_t previous;
    if (operation == NULL) return false;
    previous = atomic_exchange_explicit(
        &operation->state, ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_IDLE,
        memory_order_acq_rel);
    return previous != ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_IDLE;
}

bool esp32_mquickjs_wireless_native_operation_is_quiescent(
    const esp32_mquickjs_wireless_native_operation_t *operation)
{
    return operation == NULL || atomic_load_explicit(
        &operation->state, memory_order_acquire) ==
            ESP32_MQUICKJS_WIRELESS_NATIVE_OPERATION_IDLE;
}

bool esp32_mquickjs_wireless_close_can_release(
    uint32_t callbacks_active,
    bool native_operations_quiescent)
{
    return callbacks_active == 0U && native_operations_quiescent;
}

static bool wireless_hex_exact(const char *text, size_t length)
{
    size_t index;
    for (index = 0; index < length; ++index)
        if (wireless_hex_nibble(text[index]) < 0) return false;
    return true;
}

bool esp32_mquickjs_wireless_uuid_valid(const char *text)
{
    if (text == NULL) return false;
    if (strlen(text) == 4U) return wireless_hex_exact(text, 4U);
    if (strlen(text) == 8U) return wireless_hex_exact(text, 8U);
    if (strlen(text) != 36U || text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-') return false;
    return wireless_hex_exact(text, 8U) &&
           wireless_hex_exact(text + 9, 4U) &&
           wireless_hex_exact(text + 14, 4U) &&
           wireless_hex_exact(text + 19, 4U) &&
           wireless_hex_exact(text + 24, 12U);
}

bool esp32_mquickjs_wireless_local_value_write(
    uint8_t *value, uint16_t max_length, uint16_t *value_length,
    uint16_t offset, const uint8_t *data, uint16_t data_length)
{
    uint32_t end = (uint32_t)offset + data_length;
    if (value == NULL || value_length == NULL ||
        (data == NULL && data_length > 0U) || end > max_length) return false;
    if (data_length > 0U) memcpy(value + offset, data, data_length);
    if (end > *value_length) *value_length = (uint16_t)end;
    return true;
}

bool esp32_mquickjs_wireless_gatt_descriptor_end(
    uint16_t characteristic_index,
    uint16_t service_first_characteristic,
    uint16_t service_characteristic_count,
    uint16_t service_end_handle,
    uint16_t next_declaration_handle,
    uint16_t *out_end_handle)
{
    uint32_t service_characteristic_end =
        (uint32_t)service_first_characteristic + service_characteristic_count;

    if (out_end_handle == NULL || service_characteristic_count == 0U ||
        characteristic_index < service_first_characteristic ||
        characteristic_index >= service_characteristic_end) {
        return false;
    }
    if ((uint32_t)characteristic_index + 1U < service_characteristic_end) {
        if (next_declaration_handle == 0U) return false;
        *out_end_handle = next_declaration_handle - 1U;
    } else {
        *out_end_handle = service_end_handle;
    }
    return true;
}

bool esp32_mquickjs_wireless_generation_matches(
    uint32_t active_generation, uint32_t event_generation, bool closing)
{
    return !closing && active_generation != 0U &&
           active_generation == event_generation;
}

bool esp32_mquickjs_wireless_critical_reserve(
    esp32_mquickjs_wireless_critical_slot_t *slot,
    uint32_t generation, uint16_t index)
{
    if (slot == NULL || slot->occupied || generation == 0U) return false;
    slot->occupied = true;
    slot->generation = generation;
    slot->index = index;
    return true;
}

bool esp32_mquickjs_wireless_critical_take(
    esp32_mquickjs_wireless_critical_slot_t *slot,
    uint32_t generation, uint16_t *out_index)
{
    if (slot == NULL || out_index == NULL || !slot->occupied ||
        slot->generation != generation) return false;
    *out_index = slot->index;
    slot->occupied = false;
    slot->generation = 0U;
    slot->index = 0U;
    return true;
}
