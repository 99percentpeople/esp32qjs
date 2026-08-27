#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESP32_MQUICKJS_MEMORY_DMA_IDLE,
    ESP32_MQUICKJS_MEMORY_DMA_RESERVED,
    ESP32_MQUICKJS_MEMORY_DMA_COMMITTED,
} esp32_mquickjs_memory_dma_reservation_state_t;

typedef struct {
    esp32_mquickjs_memory_dma_reservation_state_t state;
    size_t pending_bytes;
    size_t pending_largest_bytes;
    size_t driver_pinned_bytes;
    size_t staging_pinned_bytes;
    uint32_t dma_staging_pools;
} esp32_mquickjs_memory_dma_reservation_t;

typedef struct {
    size_t pending_bytes;
    size_t pending_largest_bytes;
    size_t driver_pinned_bytes;
    size_t staging_pinned_bytes;
    uint32_t dma_staging_pools;
} esp32_mquickjs_memory_dma_accounting_t;

bool esp32_mquickjs_memory_dma_accounting_reserve(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t total_bytes,
    size_t largest_block_bytes);
bool esp32_mquickjs_memory_dma_accounting_commit(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t driver_pinned_bytes);
bool esp32_mquickjs_memory_dma_accounting_commit_staging(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t staging_pinned_bytes,
    uint32_t dma_staging_pools);
bool esp32_mquickjs_memory_dma_accounting_release(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation);
