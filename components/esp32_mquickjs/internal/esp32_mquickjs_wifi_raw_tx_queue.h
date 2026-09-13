#pragma once
#include "esp32_mquickjs_wifi_raw_tx_limits.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_NONE UINT16_MAX

typedef struct { uint8_t *data; uint16_t length; } esp32_mquickjs_wifi_raw_tx_payload_t;
typedef struct { uint32_t generation, sequence; } esp32_mquickjs_wifi_raw_tx_ticket_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_CLOSED,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED,
} esp32_mquickjs_wifi_raw_tx_queue_result_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECT_NEWEST,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH,
} esp32_mquickjs_wifi_raw_tx_queue_overflow_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FAILED,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_UNKNOWN,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECTED,
    ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_ABORTED,
} esp32_mquickjs_wifi_raw_tx_queue_outcome_t;
typedef struct {
    uint32_t admitted, submitted, settled;
    uint32_t succeeded, failed, unknown, rejected, aborted, dropped;
} esp32_mquickjs_wifi_raw_tx_queue_totals_t;
typedef struct {
    uint32_t generation, first_sequence, last_sequence, batch_sequence;
    uint16_t admitted_packets, evicted_packets, evicted_batches;
} esp32_mquickjs_wifi_raw_tx_admission_t;
typedef struct { uint32_t generation, identity; uint8_t index; } esp32_mquickjs_wifi_raw_tx_flush_token_t;
typedef struct {
    uint32_t generation, fence;
    uint16_t pending;
    esp32_mquickjs_wifi_raw_tx_queue_totals_t totals;
} esp32_mquickjs_wifi_raw_tx_flush_status_t;
typedef struct {
    esp32_mquickjs_wifi_raw_tx_payload_t payload;
    uint32_t sequence, batch_sequence;
    uint16_t next;
    bool allocated, accepted, in_flight, batch_started;
} esp32_mquickjs_wifi_raw_tx_queue_slot_t;
typedef struct {
    uint32_t identity;
    esp32_mquickjs_wifi_raw_tx_flush_status_t status;
} esp32_mquickjs_wifi_raw_tx_flush_watch_t;
typedef struct {
    bool initialized, closed;
    uint32_t generation, last_sequence, next_flush_identity, active_batch;
    uint16_t capacity, pending_head, pending_tail, active, queued, free_count;
    uint16_t max_in_flight, in_flight;
    uint32_t capacity_bytes, used_bytes, high_water_bytes;
    esp32_mquickjs_wifi_raw_tx_queue_slot_t *slots;
    esp32_mquickjs_wifi_raw_tx_queue_totals_t totals;
    esp32_mquickjs_wifi_raw_tx_flush_watch_t flushes[ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_FLUSHES];
} esp32_mquickjs_wifi_raw_tx_queue_t;

/* Pure ownership/ledger core: no SDK, allocation, freeing, callbacks or locks.
 * The native Session owner must serialize ALL access with its task mutex. Batch
 * preflight includes bounded ownership-alias scans; do not run it in an ISR or
 * an interrupt-disabled critical section. generation must come from
 * its non-reusing Session registry, and queue must be initially zeroed. Slots
 * are caller-owned control storage retained until successful deinit. */
bool esp32_mquickjs_wifi_raw_tx_queue_init(esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    esp32_mquickjs_wifi_raw_tx_queue_slot_t *slots, uint16_t capacity, uint32_t generation);
/* Configure an empty, unused queue. Limits include in-flight payloads. */
bool esp32_mquickjs_wifi_raw_tx_queue_limits(esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    uint32_t capacity_bytes, uint16_t max_in_flight);
bool esp32_mquickjs_wifi_raw_tx_queue_writable(const esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    uint16_t minimum_packets, uint32_t minimum_bytes);

/* Every payload is already captured/validated and uniquely owned by caller.
 * On success ownership moves to queue and every input descriptor is zeroed.
 * Any evicted payload moves to the empty removed[capacity] output array and must
 * be freed outside the Session lock. On failure all inputs/outputs/state remain
 * unchanged. An active batch's unsent remainder is not overflow-evictable. */
esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_queue_admit(
    esp32_mquickjs_wifi_raw_tx_queue_t *queue, esp32_mquickjs_wifi_raw_tx_payload_t *frames,
    uint16_t count, esp32_mquickjs_wifi_raw_tx_queue_overflow_t overflow,
    esp32_mquickjs_wifi_raw_tx_payload_t *removed, uint16_t removed_capacity,
    esp32_mquickjs_wifi_raw_tx_admission_t *result);

/* Borrowed payload remains owned by queue through exact native retirement.
 * Each output descriptor/token must be zero. Completion may be out of order;
 * the caller must enforce the corresponding native driver admission limit. */
bool esp32_mquickjs_wifi_raw_tx_queue_take(esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    esp32_mquickjs_wifi_raw_tx_ticket_t *ticket, esp32_mquickjs_wifi_raw_tx_payload_t *borrowed);
bool esp32_mquickjs_wifi_raw_tx_queue_accept(esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    const esp32_mquickjs_wifi_raw_tx_ticket_t *ticket);
/* Never call merely on public timeout. Caller must first prove the corresponding
 * native operation can no longer reference this payload. Callback outcomes need
 * prior accept; REJECTED needs !accept. ABORTED records explicit native teardown
 * and does not imply that accepted RF was prevented. Success transfers payload
 * to the empty retired descriptor and zeroes the exact caller ticket. */
bool esp32_mquickjs_wifi_raw_tx_queue_finish(esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    esp32_mquickjs_wifi_raw_tx_ticket_t *ticket, esp32_mquickjs_wifi_raw_tx_queue_outcome_t outcome,
    esp32_mquickjs_wifi_raw_tx_payload_t *retired);

/* Closing discards queued packets, including an active batch's unsent remainder,
 * but never drops/frees the active packet. Empty outputs have capacity >= queue
 * capacity. Repeat close is harmless after caller has consumed/cleared outputs. */
bool esp32_mquickjs_wifi_raw_tx_queue_close(esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    esp32_mquickjs_wifi_raw_tx_payload_t *removed, uint16_t removed_capacity, uint16_t *removed_count);

/* Fence covers all admissions through this call. Totals are Session-cumulative
 * for sequence <= fence; later admissions cannot delay or alter this result.
 * Each watcher remains bounded and owned until its exact token is released. */
esp32_mquickjs_wifi_raw_tx_queue_result_t esp32_mquickjs_wifi_raw_tx_queue_flush_begin(
    esp32_mquickjs_wifi_raw_tx_queue_t *queue, esp32_mquickjs_wifi_raw_tx_flush_token_t *token);
bool esp32_mquickjs_wifi_raw_tx_queue_flush_status(const esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    const esp32_mquickjs_wifi_raw_tx_flush_token_t *token, esp32_mquickjs_wifi_raw_tx_flush_status_t *status);
bool esp32_mquickjs_wifi_raw_tx_queue_flush_release(esp32_mquickjs_wifi_raw_tx_queue_t *queue,
    esp32_mquickjs_wifi_raw_tx_flush_token_t *token);
bool esp32_mquickjs_wifi_raw_tx_queue_deinit(esp32_mquickjs_wifi_raw_tx_queue_t *queue);
