#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE UINT16_MAX

typedef enum {
    ESP32_MQUICKJS_ESPNOW_TX_REJECT_NEWEST = 0,
    ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH,
} esp32_mquickjs_espnow_tx_overflow_t;

typedef struct {
    uint16_t next;
    uint32_t batch_sequence;
    bool batch_end;
    bool allocated;
} esp32_mquickjs_espnow_tx_slot_link_t;

typedef struct {
    esp32_mquickjs_espnow_tx_slot_link_t *slots;
    uint16_t capacity;
    uint16_t free_head;
    uint16_t pending_head;
    uint16_t pending_tail;
    uint16_t active;
    uint16_t free_count;
    uint16_t pending_count;
    uint32_t active_batch_sequence;
} esp32_mquickjs_espnow_tx_queue_t;

typedef struct {
    uint32_t evicted_batches;
    uint32_t evicted_packets;
} esp32_mquickjs_espnow_tx_reserve_result_t;

bool esp32_mquickjs_espnow_tx_queue_init(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    esp32_mquickjs_espnow_tx_slot_link_t *slots,
    uint16_t capacity);

bool esp32_mquickjs_espnow_tx_queue_reserve_batch(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    uint16_t packet_count,
    uint32_t batch_sequence,
    esp32_mquickjs_espnow_tx_overflow_t overflow,
    uint16_t *out_slots,
    esp32_mquickjs_espnow_tx_reserve_result_t *out_result);

void esp32_mquickjs_espnow_tx_queue_release_reserved(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    const uint16_t *reserved_slots,
    uint16_t packet_count);

bool esp32_mquickjs_espnow_tx_queue_commit_batch(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    const uint16_t *reserved_slots,
    uint16_t packet_count,
    uint32_t batch_sequence);

uint16_t esp32_mquickjs_espnow_tx_queue_start_next(
    esp32_mquickjs_espnow_tx_queue_t *queue);

uint16_t esp32_mquickjs_espnow_tx_queue_complete_active(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    bool *out_batch_completed);

uint32_t esp32_mquickjs_espnow_tx_queue_discard_pending(
    esp32_mquickjs_espnow_tx_queue_t *queue,
    uint32_t *out_batches);
