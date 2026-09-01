#include "esp32_mquickjs_espnow_tx_queue.h"

#include <string.h>

static void release_slot(esp32_mquickjs_espnow_tx_queue_t *queue,
                         uint16_t slot_index)
{
    esp32_mquickjs_espnow_tx_slot_link_t *slot = &queue->slots[slot_index];

    memset(slot, 0, sizeof(*slot));
    slot->next = queue->free_head;
    queue->free_head = slot_index;
    queue->free_count++;
}

static bool evict_oldest_pending_batch(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    esp32_mquickjs_espnow_tx_reserve_result_t *result)
{
    uint16_t previous = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    uint16_t first = queue->pending_head;
    uint32_t batch_sequence;
    uint16_t cursor;
    uint16_t after;
    uint32_t packets = 0;

    while (first != ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE &&
           queue->slots[first].batch_sequence ==
               queue->active_batch_sequence) {
        previous = first;
        first = queue->slots[first].next;
    }
    if (first == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        return false;
    }
    batch_sequence = queue->slots[first].batch_sequence;
    cursor = first;
    while (cursor != ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE &&
           queue->slots[cursor].batch_sequence == batch_sequence) {
        after = queue->slots[cursor].next;
        release_slot(queue, cursor);
        packets++;
        cursor = after;
    }
    if (previous == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        queue->pending_head = cursor;
    } else {
        queue->slots[previous].next = cursor;
    }
    if (cursor == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        queue->pending_tail = previous;
    }
    queue->pending_count -= (uint16_t)packets;
    result->evicted_batches++;
    result->evicted_packets += packets;
    return true;
}

bool esp32_mquickjs_espnow_tx_queue_init(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    esp32_mquickjs_espnow_tx_slot_link_t *slots,
    uint16_t capacity)
{
    uint16_t index;

    if (queue == NULL || slots == NULL || capacity == 0 ||
        capacity == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        return false;
    }
    memset(queue, 0, sizeof(*queue));
    memset(slots, 0, sizeof(*slots) * capacity);
    queue->slots = slots;
    queue->capacity = capacity;
    queue->free_head = 0;
    queue->pending_head = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    queue->pending_tail = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    queue->active = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    queue->free_count = capacity;
    for (index = 0; index < capacity; ++index) {
        slots[index].next = index + 1U < capacity
                                ? (uint16_t)(index + 1U)
                                : ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    }
    return true;
}

bool esp32_mquickjs_espnow_tx_queue_reserve_batch(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    uint16_t packet_count,
    uint32_t batch_sequence,
    esp32_mquickjs_espnow_tx_overflow_t overflow,
    uint16_t *out_slots,
    esp32_mquickjs_espnow_tx_reserve_result_t *out_result)
{
    esp32_mquickjs_espnow_tx_reserve_result_t result = {0};
    uint16_t index;

    if (queue == NULL || out_slots == NULL || packet_count == 0 ||
        packet_count > queue->capacity || batch_sequence == 0) {
        return false;
    }
    if (queue->free_count < packet_count &&
        overflow == ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH) {
        uint16_t cursor = queue->pending_head;
        uint16_t evictable = 0;

        while (cursor != ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
            if (queue->slots[cursor].batch_sequence !=
                queue->active_batch_sequence) {
                evictable++;
            }
            cursor = queue->slots[cursor].next;
        }
        if ((uint32_t)queue->free_count + evictable < packet_count) {
            if (out_result != NULL) {
                *out_result = result;
            }
            return false;
        }
    }
    while (queue->free_count < packet_count) {
        if (overflow != ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH ||
            !evict_oldest_pending_batch(queue, &result)) {
            if (out_result != NULL) {
                *out_result = result;
            }
            return false;
        }
    }
    for (index = 0; index < packet_count; ++index) {
        uint16_t slot_index = queue->free_head;
        esp32_mquickjs_espnow_tx_slot_link_t *slot =
            &queue->slots[slot_index];

        queue->free_head = slot->next;
        queue->free_count--;
        slot->next = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
        slot->batch_sequence = batch_sequence;
        slot->batch_end = index + 1U == packet_count;
        slot->allocated = true;
        out_slots[index] = slot_index;
    }
    if (out_result != NULL) {
        *out_result = result;
    }
    return true;
}

void esp32_mquickjs_espnow_tx_queue_release_reserved(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    const uint16_t *reserved_slots,
    uint16_t packet_count)
{
    uint16_t index;

    if (queue == NULL || reserved_slots == NULL) {
        return;
    }
    for (index = 0; index < packet_count; ++index) {
        uint16_t slot_index = reserved_slots[index];

        if (slot_index < queue->capacity &&
            queue->slots[slot_index].allocated) {
            release_slot(queue, slot_index);
        }
    }
}

bool esp32_mquickjs_espnow_tx_queue_commit_batch(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    const uint16_t *reserved_slots,
    uint16_t packet_count,
    uint32_t batch_sequence)
{
    uint16_t index;

    if (queue == NULL || reserved_slots == NULL || packet_count == 0 ||
        batch_sequence == 0) {
        return false;
    }
    for (index = 0; index < packet_count; ++index) {
        uint16_t slot_index = reserved_slots[index];

        if (slot_index >= queue->capacity ||
            !queue->slots[slot_index].allocated ||
            queue->slots[slot_index].batch_sequence != batch_sequence) {
            return false;
        }
    }
    for (index = 0; index < packet_count; ++index) {
        uint16_t slot_index = reserved_slots[index];
        uint16_t next = index + 1U < packet_count
                            ? reserved_slots[index + 1U]
                            : ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;

        queue->slots[slot_index].next = next;
    }
    if (queue->pending_tail == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        queue->pending_head = reserved_slots[0];
    } else {
        queue->slots[queue->pending_tail].next = reserved_slots[0];
    }
    queue->pending_tail = reserved_slots[packet_count - 1U];
    queue->pending_count += packet_count;
    return true;
}

uint16_t esp32_mquickjs_espnow_tx_queue_start_next(
    esp32_mquickjs_espnow_tx_queue_t *queue)
{
    uint16_t slot_index;

    if (queue == NULL ||
        queue->active != ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE ||
        queue->pending_head == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        return ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    }
    slot_index = queue->pending_head;
    queue->pending_head = queue->slots[slot_index].next;
    if (queue->pending_head == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        queue->pending_tail = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    }
    queue->slots[slot_index].next = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    queue->pending_count--;
    queue->active = slot_index;
    queue->active_batch_sequence = queue->slots[slot_index].batch_sequence;
    return slot_index;
}

uint16_t esp32_mquickjs_espnow_tx_queue_complete_active(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    bool *out_batch_completed)
{
    uint16_t slot_index;
    bool batch_completed;

    if (queue == NULL ||
        queue->active == ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        return ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    }
    slot_index = queue->active;
    batch_completed = queue->slots[slot_index].batch_end;
    queue->active = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    if (batch_completed) {
        queue->active_batch_sequence = 0;
    }
    release_slot(queue, slot_index);
    if (out_batch_completed != NULL) {
        *out_batch_completed = batch_completed;
    }
    return slot_index;
}

uint32_t esp32_mquickjs_espnow_tx_queue_discard_pending(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    uint32_t *out_batches)
{
    uint16_t cursor;
    uint32_t previous_batch = 0;
    uint32_t packets = 0;
    uint32_t batches = 0;

    if (queue == NULL) {
        return 0;
    }
    cursor = queue->pending_head;
    queue->pending_head = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    queue->pending_tail = ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE;
    queue->pending_count = 0;
    while (cursor != ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE) {
        uint16_t next = queue->slots[cursor].next;
        uint32_t batch = queue->slots[cursor].batch_sequence;

        if (batch != previous_batch) {
            batches++;
            previous_batch = batch;
        }
        release_slot(queue, cursor);
        packets++;
        cursor = next;
    }
    if (out_batches != NULL) {
        *out_batches = batches;
    }
    return packets;
}
