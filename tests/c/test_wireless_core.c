#include "esp32_mquickjs_wireless_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t pool_index;
    uint32_t sequence;
} fake_pooled_event_t;

typedef struct {
    fake_pooled_event_t events[2];
    size_t count;
    size_t sends;
} fake_event_queue_t;

static bool fake_event_queue_send_from_isr(void *destination,
                                           const void *event,
                                           int *task_woken)
{
    fake_event_queue_t *queue = destination;

    queue->sends++;
    if (queue->count >= 2) {
        return false;
    }
    queue->events[queue->count++] = *(const fake_pooled_event_t *)event;
    if (task_woken != NULL) {
        *task_woken = 1;
    }
    return true;
}

static bool fake_event_queue_try_send_from_callback(void *destination,
                                                     const void *event)
{
    fake_event_queue_t *queue = destination;

    queue->sends++;
    if (queue->count >= 2) {
        return false;
    }
    queue->events[queue->count++] = *(const fake_pooled_event_t *)event;
    return true;
}

static void test_address_and_keys(void)
{
    uint8_t address[6];
    uint8_t key[16];
    char text[18];
    memset(key, 0xa5, sizeof(key));
    assert(esp32_mquickjs_wireless_parse_address(
        "02:ab:CD:00:10:ff", address));
    assert(address[0] == 0x02 && address[1] == 0xab && address[5] == 0xff);
    assert(!esp32_mquickjs_wireless_parse_address("02:ab:cd", address));
    assert(!esp32_mquickjs_wireless_parse_address(
        "02:ab:cd:00:10:gg", address));
    esp32_mquickjs_wireless_format_address(address, text);
    assert(strcmp(text, "02:ab:cd:00:10:ff") == 0);
    assert(esp32_mquickjs_wireless_key_length_valid(16, 16));
    assert(!esp32_mquickjs_wireless_key_length_valid(15, 16));
    assert(!esp32_mquickjs_wireless_key_length_valid(17, 16));
    esp32_mquickjs_wireless_secure_zero(key, sizeof(key));
    for (size_t index = 0; index < sizeof(key); ++index) assert(key[index] == 0);
}

static void test_pool(void)
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
    assert(esp32_mquickjs_native_pool_release(&pool, 33));
    assert(!esp32_mquickjs_native_pool_release(&pool, 33));
    assert(esp32_mquickjs_native_pool_acquire(&pool, &slot));
    assert(slot == 33);
}

static void test_pooled_isr_producer_releases_rejected_slots(void)
{
    esp32_mquickjs_native_pool_t pool;
    fake_event_queue_t queue = {0};
    uint32_t dropped = 0;
    uint32_t sequence;
    int task_woken = 0;

    assert(esp32_mquickjs_native_pool_init(&pool, 4));
    for (sequence = 1; sequence <= 100; ++sequence) {
        fake_pooled_event_t event = {.sequence = sequence};

        assert(esp32_mquickjs_native_pool_acquire(
            &pool, &event.pool_index));
        if (!esp32_mquickjs_wireless_pooled_event_publish_from_isr(
                &pool, event.pool_index, &queue, &event,
                fake_event_queue_send_from_isr, &task_woken)) {
            dropped++;
        }
    }
    assert(queue.sends == 100);
    assert(queue.count == 2);
    assert(queue.events[0].sequence == 1);
    assert(queue.events[1].sequence == 2);
    assert(dropped == 98);
    assert(task_woken == 1);
    assert(esp32_mquickjs_native_pool_available(&pool) == 2);

    assert(esp32_mquickjs_native_pool_release(
        &pool, queue.events[0].pool_index));
    assert(esp32_mquickjs_native_pool_release(
        &pool, queue.events[1].pool_index));
    assert(esp32_mquickjs_native_pool_available(&pool) == 4);

    {
        uint16_t index;
        fake_pooled_event_t event = {0};

        assert(esp32_mquickjs_native_pool_acquire(&pool, &index));
        event.pool_index = index;
        assert(!esp32_mquickjs_wireless_pooled_event_publish_from_isr(
            &pool, index, NULL, &event,
            fake_event_queue_send_from_isr, NULL));
        assert(esp32_mquickjs_native_pool_available(&pool) == 4);
    }
}

static void test_pooled_callback_producer_releases_rejected_slots(void)
{
    esp32_mquickjs_native_pool_t pool;
    fake_event_queue_t queue = {0};
    uint32_t dropped = 0;
    uint32_t sequence;

    assert(esp32_mquickjs_native_pool_init(&pool, 4));
    for (sequence = 1; sequence <= 100; ++sequence) {
        fake_pooled_event_t event = {.sequence = sequence};

        assert(esp32_mquickjs_native_pool_acquire(
            &pool, &event.pool_index));
        if (!esp32_mquickjs_wireless_pooled_event_publish_from_callback(
                &pool, event.pool_index, &queue, &event,
                fake_event_queue_try_send_from_callback)) {
            dropped++;
        }
    }
    assert(queue.sends == 100);
    assert(queue.count == 2);
    assert(queue.events[0].sequence == 1);
    assert(queue.events[1].sequence == 2);
    assert(dropped == 98);
    assert(esp32_mquickjs_native_pool_available(&pool) == 2);

    assert(esp32_mquickjs_native_pool_release(
        &pool, queue.events[0].pool_index));
    assert(esp32_mquickjs_native_pool_release(
        &pool, queue.events[1].pool_index));
    assert(esp32_mquickjs_native_pool_available(&pool) == 4);
}

static void test_timeout_state(void)
{
    esp32_mquickjs_wireless_tx_state_t state =
        ESP32_MQUICKJS_WIRELESS_TX_READY;
    assert(esp32_mquickjs_wireless_tx_timeout(&state));
    assert(!esp32_mquickjs_wireless_tx_timeout(&state));
    assert(esp32_mquickjs_wireless_tx_begin_recovery(&state));
    assert(esp32_mquickjs_wireless_tx_finish_recovery(&state, true));
    assert(state == ESP32_MQUICKJS_WIRELESS_TX_READY);
    assert(esp32_mquickjs_wireless_tx_timeout(&state));
    assert(esp32_mquickjs_wireless_tx_begin_recovery(&state));
    assert(esp32_mquickjs_wireless_tx_finish_recovery(&state, false));
    assert(state == ESP32_MQUICKJS_WIRELESS_TX_FAILED);
    assert(esp32_mquickjs_wireless_tx_retry_recovery(&state));
    assert(!esp32_mquickjs_wireless_tx_retry_recovery(&state));
    assert(esp32_mquickjs_wireless_tx_finish_recovery(&state, true));
    assert(state == ESP32_MQUICKJS_WIRELESS_TX_READY);
}

static void test_native_operation_completion_lifecycle(void)
{
    esp32_mquickjs_wireless_native_operation_t operation;

    esp32_mquickjs_wireless_native_operation_init(&operation);
    assert(esp32_mquickjs_wireless_native_operation_is_quiescent(&operation));
    assert(esp32_mquickjs_wireless_native_operation_begin(&operation));
    assert(!esp32_mquickjs_wireless_native_operation_begin(&operation));
    assert(!esp32_mquickjs_wireless_native_operation_is_quiescent(&operation));
    assert(esp32_mquickjs_wireless_native_operation_request_cancel(&operation));
    assert(!esp32_mquickjs_wireless_native_operation_request_cancel(&operation));
    assert(!esp32_mquickjs_wireless_native_operation_is_quiescent(&operation));
    assert(!esp32_mquickjs_wireless_native_operation_begin(&operation));
    assert(esp32_mquickjs_wireless_native_operation_complete(&operation));
    assert(esp32_mquickjs_wireless_native_operation_is_quiescent(&operation));
    assert(!esp32_mquickjs_wireless_native_operation_complete(&operation));
    assert(esp32_mquickjs_wireless_native_operation_begin(&operation));
    assert(esp32_mquickjs_wireless_native_operation_complete(&operation));
}

static void test_close_release_gate_requires_full_quiescence(void)
{
    assert(!esp32_mquickjs_wireless_close_can_release(1, true));
    assert(!esp32_mquickjs_wireless_close_can_release(0, false));
    assert(!esp32_mquickjs_wireless_close_can_release(3, false));
    assert(esp32_mquickjs_wireless_close_can_release(0, true));
}

static void test_fixed_channel_owner_is_lease_exclusive(void)
{
    esp32_mquickjs_wireless_fixed_channel_owner_t owner = {0};
    uint32_t generation;

    assert(esp32_mquickjs_wireless_fixed_channel_check(
        &owner, 11U, 1U, 6U, 0U) ==
        ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_ACQUIRE);
    assert(esp32_mquickjs_wireless_fixed_channel_claim(
        &owner, 11U, 1U, 6U, 0U));
    generation = owner.generation;

    assert(esp32_mquickjs_wireless_fixed_channel_check(
        &owner, 11U, 1U, 6U, 0U) ==
        ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_IDEMPOTENT);
    assert(esp32_mquickjs_wireless_fixed_channel_claim(
        &owner, 11U, 1U, 6U, 0U));
    assert(owner.generation == generation);

    assert(esp32_mquickjs_wireless_fixed_channel_check(
        &owner, 12U, 1U, 6U, 0U) ==
        ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_CONFLICT);
    assert(esp32_mquickjs_wireless_fixed_channel_check(
        &owner, 13U, 2U, 6U, 0U) ==
        ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_CONFLICT);
    assert(esp32_mquickjs_wireless_fixed_channel_check(
        &owner, 13U, 2U, 11U, 0U) ==
        ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_CONFLICT);
    assert(!esp32_mquickjs_wireless_fixed_channel_release(
        &owner, 12U, 1U));
    assert(owner.claimed && owner.primary_channel == 6U &&
           owner.generation == generation);

    assert(esp32_mquickjs_wireless_fixed_channel_check(
        &owner, 11U, 1U, 11U, 0U) ==
        ESP32_MQUICKJS_WIRELESS_FIXED_CHANNEL_CHANGE);
    assert(esp32_mquickjs_wireless_fixed_channel_claim(
        &owner, 11U, 1U, 11U, 0U));
    assert(owner.generation != generation && owner.primary_channel == 11U);
    assert(!esp32_mquickjs_wireless_fixed_channel_release(
        &owner, 13U, 2U));
    assert(esp32_mquickjs_wireless_fixed_channel_release(
        &owner, 11U, 1U));
    assert(!owner.claimed);
}

static void test_ble_helpers(void)
{
    uint8_t value[8] = {0};
    uint16_t value_length = 2;
    uint8_t update[] = {3, 4, 5};
    esp32_mquickjs_wireless_critical_slot_t critical = {0};
    uint16_t index;
    uint16_t descriptor_end;
    assert(esp32_mquickjs_wireless_uuid_valid("180f"));
    assert(esp32_mquickjs_wireless_uuid_valid("12345678"));
    assert(esp32_mquickjs_wireless_uuid_valid(
        "12345678-1234-5678-9abc-def012345678"));
    assert(!esp32_mquickjs_wireless_uuid_valid("1234-5678"));
    assert(esp32_mquickjs_wireless_local_value_write(
        value, sizeof(value), &value_length, 2, update, sizeof(update)));
    assert(value_length == 5 && value[2] == 3 && value[4] == 5);
    assert(!esp32_mquickjs_wireless_local_value_write(
        value, sizeof(value), &value_length, 7, update, sizeof(update)));
    assert(esp32_mquickjs_wireless_generation_matches(9, 9, false));
    assert(!esp32_mquickjs_wireless_generation_matches(9, 9, true));
    assert(!esp32_mquickjs_wireless_generation_matches(9, 8, false));
    assert(esp32_mquickjs_wireless_critical_reserve(&critical, 7, 3));
    assert(!esp32_mquickjs_wireless_critical_reserve(&critical, 7, 4));
    assert(!esp32_mquickjs_wireless_critical_take(&critical, 8, &index));
    assert(esp32_mquickjs_wireless_critical_take(&critical, 7, &index));
    assert(index == 3);
    assert(esp32_mquickjs_wireless_gatt_descriptor_end(
        1, 0, 3, 12, 9, &descriptor_end));
    assert(descriptor_end == 8);
    assert(esp32_mquickjs_wireless_gatt_descriptor_end(
        2, 0, 3, 7, 9, &descriptor_end));
    assert(descriptor_end == 7);
    assert(!esp32_mquickjs_wireless_gatt_descriptor_end(
        0, 0, 0, 7, 0, &descriptor_end));
}

int main(void)
{
    test_address_and_keys();
    test_pool();
    test_pooled_isr_producer_releases_rejected_slots();
    test_pooled_callback_producer_releases_rejected_slots();
    test_timeout_state();
    test_native_operation_completion_lifecycle();
    test_close_release_gate_requires_full_quiescence();
    test_fixed_channel_owner_is_lease_exclusive();
    test_ble_helpers();
    puts("wireless core tests passed");
    return 0;
}
