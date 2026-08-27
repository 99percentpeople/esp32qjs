#include "esp32_mquickjs_memory_dma_accounting.h"

#include <stdint.h>
#include <string.h>

static bool memory_size_add_fits(size_t left, size_t right)
{
    return left <= SIZE_MAX - right;
}

bool esp32_mquickjs_memory_dma_accounting_reserve(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t total_bytes,
    size_t largest_block_bytes)
{
    if (totals == NULL || reservation == NULL || total_bytes == 0 ||
        largest_block_bytes == 0 || largest_block_bytes > total_bytes ||
        reservation->state != ESP32_MQUICKJS_MEMORY_DMA_IDLE ||
        !memory_size_add_fits(totals->pending_bytes, total_bytes) ||
        !memory_size_add_fits(totals->pending_largest_bytes,
                              largest_block_bytes)) {
        return false;
    }
    totals->pending_bytes += total_bytes;
    totals->pending_largest_bytes += largest_block_bytes;
    reservation->state = ESP32_MQUICKJS_MEMORY_DMA_RESERVED;
    reservation->pending_bytes = total_bytes;
    reservation->pending_largest_bytes = largest_block_bytes;
    return true;
}

bool esp32_mquickjs_memory_dma_accounting_commit(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t driver_pinned_bytes)
{
    if (totals == NULL || reservation == NULL || driver_pinned_bytes == 0 ||
        reservation->state != ESP32_MQUICKJS_MEMORY_DMA_RESERVED ||
        driver_pinned_bytes > reservation->pending_bytes ||
        totals->pending_bytes < reservation->pending_bytes ||
        totals->pending_largest_bytes <
            reservation->pending_largest_bytes ||
        !memory_size_add_fits(totals->driver_pinned_bytes,
                              driver_pinned_bytes)) {
        return false;
    }
    totals->pending_bytes -= reservation->pending_bytes;
    totals->pending_largest_bytes -= reservation->pending_largest_bytes;
    totals->driver_pinned_bytes += driver_pinned_bytes;
    reservation->state = ESP32_MQUICKJS_MEMORY_DMA_COMMITTED;
    reservation->pending_bytes = 0;
    reservation->pending_largest_bytes = 0;
    reservation->driver_pinned_bytes = driver_pinned_bytes;
    return true;
}

bool esp32_mquickjs_memory_dma_accounting_commit_staging(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation,
    size_t staging_pinned_bytes,
    uint32_t dma_staging_pools)
{
    if (totals == NULL || reservation == NULL ||
        staging_pinned_bytes == 0 || dma_staging_pools == 0 ||
        reservation->state != ESP32_MQUICKJS_MEMORY_DMA_RESERVED ||
        staging_pinned_bytes > reservation->pending_bytes ||
        totals->pending_bytes < reservation->pending_bytes ||
        totals->pending_largest_bytes < reservation->pending_largest_bytes ||
        !memory_size_add_fits(totals->staging_pinned_bytes,
                              staging_pinned_bytes) ||
        totals->dma_staging_pools > UINT32_MAX - dma_staging_pools) {
        return false;
    }
    totals->pending_bytes -= reservation->pending_bytes;
    totals->pending_largest_bytes -= reservation->pending_largest_bytes;
    totals->staging_pinned_bytes += staging_pinned_bytes;
    totals->dma_staging_pools += dma_staging_pools;
    reservation->state = ESP32_MQUICKJS_MEMORY_DMA_COMMITTED;
    reservation->pending_bytes = 0;
    reservation->pending_largest_bytes = 0;
    reservation->staging_pinned_bytes = staging_pinned_bytes;
    reservation->dma_staging_pools = dma_staging_pools;
    return true;
}

bool esp32_mquickjs_memory_dma_accounting_release(
    esp32_mquickjs_memory_dma_accounting_t *totals,
    esp32_mquickjs_memory_dma_reservation_t *reservation)
{
    if (totals == NULL || reservation == NULL ||
        reservation->state == ESP32_MQUICKJS_MEMORY_DMA_IDLE) {
        return false;
    }
    if (reservation->state == ESP32_MQUICKJS_MEMORY_DMA_RESERVED) {
        if (totals->pending_bytes < reservation->pending_bytes ||
            totals->pending_largest_bytes <
                reservation->pending_largest_bytes) {
            return false;
        }
        totals->pending_bytes -= reservation->pending_bytes;
        totals->pending_largest_bytes -= reservation->pending_largest_bytes;
    } else if (reservation->state == ESP32_MQUICKJS_MEMORY_DMA_COMMITTED) {
        if (totals->driver_pinned_bytes < reservation->driver_pinned_bytes ||
            totals->staging_pinned_bytes <
                reservation->staging_pinned_bytes ||
            totals->dma_staging_pools < reservation->dma_staging_pools) {
            return false;
        }
        totals->driver_pinned_bytes -= reservation->driver_pinned_bytes;
        totals->staging_pinned_bytes -= reservation->staging_pinned_bytes;
        totals->dma_staging_pools -= reservation->dma_staging_pools;
    } else {
        return false;
    }
    memset(reservation, 0, sizeof(*reservation));
    return true;
}
