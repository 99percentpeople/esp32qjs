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
uint64_t esp32_mquickjs_dma_progress_timeout_us(size_t bytes,
                                                uint32_t actual_freq_hz);
