#include "esp32_mquickjs_espnow_tx_queue.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static void commit_batch(esp32_mquickjs_espnow_tx_queue_t *queue,
                         uint16_t count,
                         uint32_t sequence,
                         esp32_mquickjs_espnow_tx_overflow_t overflow,
                         esp32_mquickjs_espnow_tx_reserve_result_t *result)
{
    uint16_t slots[8];

    assert(esp32_mquickjs_espnow_tx_queue_reserve_batch(
        queue, count, sequence, overflow, slots, result));
    assert(esp32_mquickjs_espnow_tx_queue_commit_batch(
        queue, slots, count, sequence));
}

static void test_reject_is_atomic(void)
{
    esp32_mquickjs_espnow_tx_slot_link_t slots[4];
    esp32_mquickjs_espnow_tx_queue_t queue;
    esp32_mquickjs_espnow_tx_reserve_result_t result = {0};
    uint16_t reserved[4];

    assert(esp32_mquickjs_espnow_tx_queue_init(&queue, slots, 4));
    commit_batch(&queue, 3, 1,
                 ESP32_MQUICKJS_ESPNOW_TX_REJECT_NEWEST, &result);
    assert(!esp32_mquickjs_espnow_tx_queue_reserve_batch(
        &queue, 2, 2, ESP32_MQUICKJS_ESPNOW_TX_REJECT_NEWEST,
        reserved, &result));
    assert(queue.pending_count == 3);
    assert(queue.free_count == 1);
    assert(result.evicted_batches == 0);
}

static void test_drop_oldest_preserves_whole_batches(void)
{
    esp32_mquickjs_espnow_tx_slot_link_t slots[6];
    esp32_mquickjs_espnow_tx_queue_t queue;
    esp32_mquickjs_espnow_tx_reserve_result_t result = {0};

    assert(esp32_mquickjs_espnow_tx_queue_init(&queue, slots, 6));
    commit_batch(&queue, 2, 1,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    commit_batch(&queue, 2, 2,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    commit_batch(&queue, 2, 3,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    result = (esp32_mquickjs_espnow_tx_reserve_result_t){0};
    commit_batch(&queue, 3, 4,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    assert(result.evicted_batches == 2);
    assert(result.evicted_packets == 4);
    assert(queue.pending_count == 5);
}

static void test_active_batch_is_never_evicted(void)
{
    esp32_mquickjs_espnow_tx_slot_link_t slots[6];
    esp32_mquickjs_espnow_tx_queue_t queue;
    esp32_mquickjs_espnow_tx_reserve_result_t result = {0};
    uint16_t active;
    bool batch_completed = false;

    assert(esp32_mquickjs_espnow_tx_queue_init(&queue, slots, 6));
    commit_batch(&queue, 3, 10,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    commit_batch(&queue, 3, 11,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    active = esp32_mquickjs_espnow_tx_queue_start_next(&queue);
    assert(active != ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE);
    assert(slots[active].batch_sequence == 10);
    result = (esp32_mquickjs_espnow_tx_reserve_result_t){0};
    commit_batch(&queue, 3, 12,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    assert(result.evicted_batches == 1);
    assert(result.evicted_packets == 3);
    assert(queue.active_batch_sequence == 10);
    assert(esp32_mquickjs_espnow_tx_queue_complete_active(
               &queue, &batch_completed) == active);
    assert(!batch_completed);
    active = esp32_mquickjs_espnow_tx_queue_start_next(&queue);
    assert(slots[active].batch_sequence == 10);
}

static void test_release_and_discard_return_every_slot(void)
{
    esp32_mquickjs_espnow_tx_slot_link_t slots[4];
    esp32_mquickjs_espnow_tx_queue_t queue;
    esp32_mquickjs_espnow_tx_reserve_result_t result = {0};
    uint16_t reserved[2];
    uint32_t batches = 0;

    assert(esp32_mquickjs_espnow_tx_queue_init(&queue, slots, 4));
    assert(esp32_mquickjs_espnow_tx_queue_reserve_batch(
        &queue, 2, 20, ESP32_MQUICKJS_ESPNOW_TX_REJECT_NEWEST,
        reserved, &result));
    esp32_mquickjs_espnow_tx_queue_release_reserved(&queue, reserved, 2);
    assert(queue.free_count == 4);
    commit_batch(&queue, 2, 21,
                 ESP32_MQUICKJS_ESPNOW_TX_REJECT_NEWEST, &result);
    commit_batch(&queue, 2, 22,
                 ESP32_MQUICKJS_ESPNOW_TX_REJECT_NEWEST, &result);
    assert(esp32_mquickjs_espnow_tx_queue_discard_pending(
               &queue, &batches) == 4);
    assert(batches == 2);
    assert(queue.free_count == 4);
}

static void test_failed_drop_reservation_does_not_evict(void)
{
    esp32_mquickjs_espnow_tx_slot_link_t slots[4];
    esp32_mquickjs_espnow_tx_queue_t queue;
    esp32_mquickjs_espnow_tx_reserve_result_t result = {0};
    uint16_t reserved[4];

    assert(esp32_mquickjs_espnow_tx_queue_init(&queue, slots, 4));
    commit_batch(&queue, 3, 30,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    assert(esp32_mquickjs_espnow_tx_queue_start_next(&queue) !=
           ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE);
    commit_batch(&queue, 1, 31,
                 ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH, &result);
    result = (esp32_mquickjs_espnow_tx_reserve_result_t){0};
    assert(!esp32_mquickjs_espnow_tx_queue_reserve_batch(
        &queue, 4, 32, ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH,
        reserved, &result));
    assert(result.evicted_batches == 0);
    assert(queue.pending_count == 3);
    assert(queue.free_count == 0);
}

int main(void)
{
    test_reject_is_atomic();
    test_drop_oldest_preserves_whole_batches();
    test_active_batch_is_never_evicted();
    test_release_and_discard_return_every_slot();
    test_failed_drop_reservation_does_not_evict();
    puts("espnow tx queue tests passed");
    return 0;
}
