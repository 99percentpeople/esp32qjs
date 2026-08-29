#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT 2U
#define ESP32_MQUICKJS_DMA_NO_SLOT UINT8_MAX

typedef enum {
    ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL = 0,
    ESP32_MQUICKJS_DMA_PATH_DIRECT_EXTERNAL,
    ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL,
    ESP32_MQUICKJS_DMA_PATH_MIXED,
} esp32_mquickjs_dma_path_t;

typedef enum {
    ESP32_MQUICKJS_DMA_PROGRESS_OK = 0,
    ESP32_MQUICKJS_DMA_PROGRESS_UNDERFLOW,
    ESP32_MQUICKJS_DMA_PROGRESS_OVERFLOW,
} esp32_mquickjs_dma_progress_t;

typedef enum {
    ESP32_MQUICKJS_DMA_COMPLETION_INVALID = 0,
    ESP32_MQUICKJS_DMA_COMPLETION_IN_ORDER,
    ESP32_MQUICKJS_DMA_COMPLETION_OUT_OF_ORDER,
} esp32_mquickjs_dma_completion_t;

typedef enum {
    ESP32_MQUICKJS_DMA_CANCEL_REJECTED = 0,
    ESP32_MQUICKJS_DMA_CANCEL_COMPLETE,
    ESP32_MQUICKJS_DMA_CANCEL_PENDING,
} esp32_mquickjs_dma_cancel_t;

typedef enum {
    ESP32_MQUICKJS_DMA_QUEUE_SLOT_FREE = 0,
    ESP32_MQUICKJS_DMA_QUEUE_SLOT_SUBMITTED,
    ESP32_MQUICKJS_DMA_QUEUE_SLOT_RETURNED,
} esp32_mquickjs_dma_queue_slot_state_t;

typedef struct {
    uint8_t *tx;
    uint8_t *rx;
    bool in_use;
} esp32_mquickjs_dma_slot_t;

typedef struct {
    esp32_mquickjs_dma_slot_t slots[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT];
    size_t capacity;
    uint8_t next_slot;
} esp32_mquickjs_dma_workspace_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    uint32_t source_span;
    esp32_mquickjs_dma_path_t path;
} esp32_mquickjs_dma_cursor_t;

typedef struct {
    const uint8_t *tx_data;
    uint8_t *rx_data;
    size_t length;
    size_t span_offset;
    uint32_t source_span;
    esp32_mquickjs_dma_path_t path;
    uint8_t slot_index;
    bool uses_staging;
    bool keep_cs_active;
} esp32_mquickjs_dma_chunk_t;

typedef struct {
    size_t bytes;
    size_t staged_bytes;
    uint32_t source_spans;
    uint32_t transactions;
    uint8_t path_mask;
} esp32_mquickjs_dma_stats_t;

/**
 * Bounded completion state shared by DMA adapters that use the two fixed
 * staging slots. A returned out-of-order slot remains occupied until every
 * earlier submission has returned, preventing premature slot reuse.
 */
typedef struct {
    uint32_t depth;
    uint32_t head;
    uint32_t tail;
    uint32_t occupied;
    uint32_t in_flight;
    esp32_mquickjs_dma_queue_slot_state_t
        slots[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT];
} esp32_mquickjs_dma_completion_queue_t;

typedef void *(*esp32_mquickjs_dma_buffer_allocate_fn)(
    size_t bytes,
    void *opaque);
typedef void (*esp32_mquickjs_dma_buffer_release_fn)(
    void *buffer,
    void *opaque);

esp32_mquickjs_dma_path_t esp32_mquickjs_dma_classify_source(
    bool dma_capable,
    bool external,
    bool direct_external_dma);

void esp32_mquickjs_dma_workspace_init(
    esp32_mquickjs_dma_workspace_t *workspace,
    uint8_t *first_tx,
    uint8_t *first_rx,
    uint8_t *second_tx,
    uint8_t *second_rx,
    size_t capacity);
void esp32_mquickjs_dma_workspace_release(
    esp32_mquickjs_dma_workspace_t *workspace,
    uint8_t slot_index);
bool esp32_mquickjs_dma_workspace_allocate_buffers(
    uint8_t *tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    uint8_t *rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    size_t bytes,
    esp32_mquickjs_dma_buffer_allocate_fn allocate,
    esp32_mquickjs_dma_buffer_release_fn release,
    void *opaque);
void esp32_mquickjs_dma_workspace_release_buffers(
    uint8_t *tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    uint8_t *rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT],
    esp32_mquickjs_dma_buffer_release_fn release,
    void *opaque);

void esp32_mquickjs_dma_cursor_begin(
    esp32_mquickjs_dma_cursor_t *cursor,
    const uint8_t *data,
    size_t length,
    esp32_mquickjs_dma_path_t path,
    uint32_t source_span);
bool esp32_mquickjs_dma_cursor_next(
    esp32_mquickjs_dma_cursor_t *cursor,
    esp32_mquickjs_dma_workspace_t *workspace,
    size_t max_transfer_size,
    bool transmit,
    bool receive,
    esp32_mquickjs_dma_chunk_t *out);

void esp32_mquickjs_dma_stats_init(esp32_mquickjs_dma_stats_t *stats);
void esp32_mquickjs_dma_stats_note_span(
    esp32_mquickjs_dma_stats_t *stats,
    esp32_mquickjs_dma_path_t path);
void esp32_mquickjs_dma_stats_note_transaction(
    esp32_mquickjs_dma_stats_t *stats,
    esp32_mquickjs_dma_path_t path,
    size_t completed_bytes,
    size_t staged_bytes);
esp32_mquickjs_dma_path_t esp32_mquickjs_dma_stats_path(
    const esp32_mquickjs_dma_stats_t *stats);
const char *esp32_mquickjs_dma_path_name(esp32_mquickjs_dma_path_t path);

esp32_mquickjs_dma_progress_t esp32_mquickjs_dma_validate_tx_progress(
    size_t actual_bytes,
    size_t expected_bytes);
esp32_mquickjs_dma_progress_t esp32_mquickjs_dma_validate_rx_progress(
    size_t actual_bytes,
    size_t expected_bytes);
bool esp32_mquickjs_dma_rx_window_fits(
    size_t output_offset,
    size_t completed_bytes,
    size_t output_capacity);

bool esp32_mquickjs_dma_completion_queue_init(
    esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t depth);
bool esp32_mquickjs_dma_completion_queue_can_submit(
    const esp32_mquickjs_dma_completion_queue_t *queue);
bool esp32_mquickjs_dma_completion_queue_next_submit(
    const esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t *out_index);
bool esp32_mquickjs_dma_completion_queue_note_submitted(
    esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t index);
esp32_mquickjs_dma_completion_t
esp32_mquickjs_dma_completion_queue_note_returned(
    esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t index);
bool esp32_mquickjs_dma_completion_queue_peek_in_flight(
    const esp32_mquickjs_dma_completion_queue_t *queue,
    uint32_t *out_index);
bool esp32_mquickjs_dma_completion_queue_releasable(
    const esp32_mquickjs_dma_completion_queue_t *queue);
esp32_mquickjs_dma_cancel_t esp32_mquickjs_dma_cancel_disposition(
    const esp32_mquickjs_dma_completion_queue_t *queue,
    bool completed,
    bool cancellation_requested);

uint64_t esp32_mquickjs_dma_progress_timeout_us(size_t bytes,
                                                uint32_t actual_freq_hz);
bool esp32_mquickjs_dma_progress_timed_out(
    uint64_t now_us,
    uint64_t deadline_us,
    bool completion_observed);
