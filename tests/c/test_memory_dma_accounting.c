#include "esp32_mquickjs_memory_dma_accounting.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void test_pending_commit_release(void)
{
    esp32_mquickjs_memory_dma_accounting_t totals = {0};
    esp32_mquickjs_memory_dma_reservation_t reservation = {0};

    assert(esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 4096, 1024));
    assert(totals.pending_bytes == 4096);
    assert(totals.pending_largest_bytes == 1024);
    assert(totals.driver_pinned_bytes == 0);
    assert(reservation.state == ESP32_MQUICKJS_MEMORY_DMA_RESERVED);

    assert(esp32_mquickjs_memory_dma_accounting_commit(
        &totals, &reservation, 2880));
    assert(totals.pending_bytes == 0);
    assert(totals.pending_largest_bytes == 0);
    assert(totals.driver_pinned_bytes == 2880);
    assert(reservation.state == ESP32_MQUICKJS_MEMORY_DMA_COMMITTED);

    assert(esp32_mquickjs_memory_dma_accounting_release(
        &totals, &reservation));
    assert(totals.pending_bytes == 0);
    assert(totals.pending_largest_bytes == 0);
    assert(totals.driver_pinned_bytes == 0);
    assert(reservation.state == ESP32_MQUICKJS_MEMORY_DMA_IDLE);
}

static void test_pending_failure_release(void)
{
    esp32_mquickjs_memory_dma_accounting_t totals = {0};
    esp32_mquickjs_memory_dma_reservation_t reservation = {0};

    assert(esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 8192, 2048));
    assert(esp32_mquickjs_memory_dma_accounting_release(
        &totals, &reservation));
    assert(totals.pending_bytes == 0);
    assert(totals.pending_largest_bytes == 0);
    assert(totals.driver_pinned_bytes == 0);
}

static void test_invalid_transitions_do_not_change_totals(void)
{
    esp32_mquickjs_memory_dma_accounting_t totals = {0};
    esp32_mquickjs_memory_dma_reservation_t reservation = {0};

    assert(!esp32_mquickjs_memory_dma_accounting_commit(
        &totals, &reservation, 1));
    assert(!esp32_mquickjs_memory_dma_accounting_release(
        &totals, &reservation));
    assert(esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 1024, 512));
    assert(!esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 1024, 512));
    assert(totals.pending_bytes == 1024);
    assert(totals.pending_largest_bytes == 512);
    assert(totals.driver_pinned_bytes == 0);
}

static void test_commit_must_fit_reservation(void)
{
    esp32_mquickjs_memory_dma_accounting_t totals = {0};
    esp32_mquickjs_memory_dma_reservation_t reservation = {0};

    assert(esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 1024, 512));
    assert(!esp32_mquickjs_memory_dma_accounting_commit(
        &totals, &reservation, 1025));
    assert(totals.pending_bytes == 1024);
    assert(totals.driver_pinned_bytes == 0);
    assert(esp32_mquickjs_memory_dma_accounting_release(
        &totals, &reservation));
}

static void test_reservation_overflow_is_rejected(void)
{
    esp32_mquickjs_memory_dma_accounting_t totals = {
        .pending_bytes = SIZE_MAX,
        .pending_largest_bytes = SIZE_MAX,
    };
    esp32_mquickjs_memory_dma_reservation_t reservation = {0};

    assert(!esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 1, 1));
    assert(totals.pending_bytes == SIZE_MAX);
    assert(totals.pending_largest_bytes == SIZE_MAX);
    assert(reservation.state == ESP32_MQUICKJS_MEMORY_DMA_IDLE);
}

static void test_staging_commit_and_release(void)
{
    esp32_mquickjs_memory_dma_accounting_t totals = {0};
    esp32_mquickjs_memory_dma_reservation_t reservation = {0};

    assert(esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 32768, 8192));
    assert(esp32_mquickjs_memory_dma_accounting_commit_staging(
        &totals, &reservation, 32768, 1));
    assert(totals.driver_pinned_bytes == 0);
    assert(totals.staging_pinned_bytes == 32768);
    assert(totals.dma_staging_pools == 1);
    assert(esp32_mquickjs_memory_dma_accounting_release(
        &totals, &reservation));
    assert(totals.staging_pinned_bytes == 0);
    assert(totals.dma_staging_pools == 0);
}

static void test_staging_partial_allocation_rollback(void)
{
    esp32_mquickjs_memory_dma_accounting_t totals = {0};
    esp32_mquickjs_memory_dma_reservation_t reservation = {0};

    assert(esp32_mquickjs_memory_dma_accounting_reserve(
        &totals, &reservation, 32768, 8192));
    assert(esp32_mquickjs_memory_dma_accounting_release(
        &totals, &reservation));
    assert(totals.pending_bytes == 0);
    assert(totals.pending_largest_bytes == 0);
    assert(totals.staging_pinned_bytes == 0);
    assert(totals.dma_staging_pools == 0);
}

int main(void)
{
    test_pending_commit_release();
    test_pending_failure_release();
    test_invalid_transitions_do_not_change_totals();
    test_commit_must_fit_reservation();
    test_reservation_overflow_is_rejected();
    test_staging_commit_and_release();
    test_staging_partial_allocation_rollback();
    return 0;
}
