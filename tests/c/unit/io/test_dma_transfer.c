#include "esp32_mquickjs_dma_transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void expect_size(size_t actual, size_t expected, const char *message)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %zu, got %zu)\n",
                message, expected, actual);
        exit(1);
    }
}

typedef struct {
    size_t allocation_calls;
    size_t release_calls;
    size_t live_buffers;
    size_t fail_call;
} allocation_probe_t;

static void *probe_allocate(size_t bytes, void *opaque)
{
    allocation_probe_t *probe = opaque;
    void *buffer;

    probe->allocation_calls++;
    if (probe->allocation_calls == probe->fail_call) {
        return NULL;
    }
    buffer = malloc(bytes);
    if (buffer != NULL) {
        probe->live_buffers++;
    }
    return buffer;
}

static void probe_release(void *buffer, void *opaque)
{
    allocation_probe_t *probe = opaque;

    free(buffer);
    probe->release_calls++;
    probe->live_buffers--;
}

static void test_source_classifier(void)
{
    expect_true(esp32_mquickjs_dma_classify_source(true, false, false) ==
                    ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL,
                "internal DMA source should be direct");
    expect_true(esp32_mquickjs_dma_classify_source(true, true, false) ==
                    ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL,
                "external DMA source should stage by default");
    expect_true(esp32_mquickjs_dma_classify_source(true, true, true) ==
                    ESP32_MQUICKJS_DMA_PATH_DIRECT_EXTERNAL,
                "explicit external DMA source should be direct");
    expect_true(esp32_mquickjs_dma_classify_source(false, false, true) ==
                    ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL,
                "non-DMA source should always stage");
}

static void test_empty_odd_and_long_spans(void)
{
    uint8_t first_tx[5] = {0};
    uint8_t first_rx[5] = {0};
    uint8_t second_tx[5] = {0};
    uint8_t second_rx[5] = {0};
    uint8_t source[13];
    esp32_mquickjs_dma_workspace_t workspace;
    esp32_mquickjs_dma_cursor_t cursor;
    esp32_mquickjs_dma_chunk_t chunk;

    memset(source, 0xa5, sizeof(source));
    esp32_mquickjs_dma_workspace_init(
        &workspace, first_tx, first_rx, second_tx, second_rx, 5);
    esp32_mquickjs_dma_cursor_begin(
        &cursor, source, 0, ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL, 1);
    expect_true(!esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, 13, true, false, &chunk),
                "empty span should not produce a transaction");

    esp32_mquickjs_dma_cursor_begin(
        &cursor, source, sizeof(source),
        ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL, 2);
    expect_true(esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &chunk),
                "long staged span first chunk");
    expect_size(chunk.length, 5, "first staged length");
    expect_true(chunk.keep_cs_active, "first staged piece keeps CS active");
    expect_true(chunk.slot_index == 0, "first staged piece uses first slot");
    expect_true(memcmp(chunk.tx_data, source, 5) == 0,
                "first staged piece should be copied");
    esp32_mquickjs_dma_workspace_release(&workspace, chunk.slot_index);

    expect_true(esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &chunk),
                "long staged span second chunk");
    expect_size(chunk.length, 5, "second staged length");
    expect_true(chunk.slot_index == 1, "second staged piece rotates slots");
    esp32_mquickjs_dma_workspace_release(&workspace, chunk.slot_index);

    expect_true(esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &chunk),
                "long staged span odd tail");
    expect_size(chunk.length, 3, "odd staged tail length");
    expect_true(!chunk.keep_cs_active, "final staged piece releases CS");
    esp32_mquickjs_dma_workspace_release(&workspace, chunk.slot_index);
    expect_true(!esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &chunk),
                "exhausted cursor should stop");
}

static void test_slot_rotation_and_contention(void)
{
    uint8_t tx0[4] = {0};
    uint8_t rx0[4] = {0};
    uint8_t tx1[4] = {0};
    uint8_t rx1[4] = {0};
    uint8_t source[12] = {0};
    esp32_mquickjs_dma_workspace_t workspace;
    esp32_mquickjs_dma_cursor_t cursor;
    esp32_mquickjs_dma_chunk_t first;
    esp32_mquickjs_dma_chunk_t second;
    esp32_mquickjs_dma_chunk_t blocked;

    esp32_mquickjs_dma_workspace_init(
        &workspace, tx0, rx0, tx1, rx1, 4);
    esp32_mquickjs_dma_cursor_begin(
        &cursor, source, sizeof(source),
        ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL, 1);
    expect_true(esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &first),
                "first slot acquisition");
    expect_true(esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &second),
                "second slot acquisition");
    expect_true(!esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &blocked),
                "both staging slots busy should apply backpressure");
    esp32_mquickjs_dma_workspace_release(&workspace, first.slot_index);
    expect_true(esp32_mquickjs_dma_cursor_next(
                    &cursor, &workspace, sizeof(source), true, false, &blocked),
                "released slot should be reusable");
    expect_true(blocked.slot_index == first.slot_index,
                "rotation should reuse the released slot");
    esp32_mquickjs_dma_workspace_release(&workspace, second.slot_index);
    esp32_mquickjs_dma_workspace_release(&workspace, blocked.slot_index);
}

static void test_workspace_allocation_failure_is_all_or_nothing(void)
{
    size_t fail_call;

    for (fail_call = 1; fail_call <= 4; ++fail_call) {
        uint8_t *tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT] = {0};
        uint8_t *rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT] = {0};
        allocation_probe_t probe = {.fail_call = fail_call};
        size_t index;

        expect_true(!esp32_mquickjs_dma_workspace_allocate_buffers(
                        tx, rx, 32, probe_allocate, probe_release, &probe),
                    "injected staging allocation failure should reject");
        expect_size(probe.allocation_calls, fail_call,
                    "allocator stops at injected failure");
        expect_size(probe.release_calls, fail_call - 1,
                    "every prior allocation is released exactly once");
        expect_size(probe.live_buffers, 0,
                    "failed staging allocation leaves no live buffers");
        for (index = 0;
             index < ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT; ++index) {
            expect_true(tx[index] == NULL && rx[index] == NULL,
                        "failed staging allocation clears every slot");
        }
    }

    {
        uint8_t *tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT] = {0};
        uint8_t *rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT] = {0};
        allocation_probe_t probe = {.fail_call = SIZE_MAX};

        expect_true(esp32_mquickjs_dma_workspace_allocate_buffers(
                        tx, rx, 32, probe_allocate, probe_release, &probe),
                    "complete staging allocation should succeed");
        expect_size(probe.live_buffers, 4,
                    "two TX and two RX buffers remain live");
        esp32_mquickjs_dma_workspace_release_buffers(
            tx, rx, probe_release, &probe);
        expect_size(probe.release_calls, 4,
                    "successful staging workspace releases every buffer");
        expect_size(probe.live_buffers, 0,
                    "successful staging workspace leaves no leak on close");
    }
}

static void test_mixed_stats_and_progress_validation(void)
{
    esp32_mquickjs_dma_stats_t stats;

    esp32_mquickjs_dma_stats_init(&stats);
    esp32_mquickjs_dma_stats_note_span(
        &stats, ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL);
    esp32_mquickjs_dma_stats_note_transaction(
        &stats, ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL, 8, 0);
    esp32_mquickjs_dma_stats_note_span(
        &stats, ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL);
    esp32_mquickjs_dma_stats_note_transaction(
        &stats, ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL, 5, 5);
    expect_true(esp32_mquickjs_dma_stats_path(&stats) ==
                    ESP32_MQUICKJS_DMA_PATH_MIXED,
                "multiple paths should report mixed");
    expect_size(stats.source_spans, 2, "source span count");
    expect_size(stats.transactions, 2, "transaction count");
    expect_size(stats.bytes, 13, "completed byte count");
    expect_size(stats.staged_bytes, 5, "staged byte count");

    expect_true(esp32_mquickjs_dma_validate_tx_progress(12, 12) ==
                    ESP32_MQUICKJS_DMA_PROGRESS_OK,
                "complete TX progress");
    expect_true(esp32_mquickjs_dma_validate_tx_progress(11, 12) ==
                    ESP32_MQUICKJS_DMA_PROGRESS_UNDERFLOW,
                "short TX progress should underflow");
    expect_true(esp32_mquickjs_dma_validate_rx_progress(13, 12) ==
                    ESP32_MQUICKJS_DMA_PROGRESS_OVERFLOW,
                "long RX progress should overflow");
    expect_true(esp32_mquickjs_dma_rx_window_fits(5, 7, 12),
                "exact RX output window should fit");
    expect_true(!esp32_mquickjs_dma_rx_window_fits(6, 7, 12),
                "RX bytes beyond output capacity should overflow");
    expect_true(!esp32_mquickjs_dma_rx_window_fits(13, 0, 12),
                "RX offset beyond output capacity should overflow");

    expect_size(
        (size_t)esp32_mquickjs_dma_progress_timeout_us(1, 80000000),
        100000,
        "small transfers should use the 100 ms progress floor");
    expect_size(
        (size_t)esp32_mquickjs_dma_progress_timeout_us(1000000, 1000000),
        32050000,
        "large transfers should use four wire times plus 50 ms");
}

static void test_completion_order_and_close_pending_ownership(void)
{
    esp32_mquickjs_dma_completion_queue_t queue;
    uint32_t index = UINT32_MAX;

    expect_true(!esp32_mquickjs_dma_completion_queue_init(&queue, 0),
                "zero-depth completion queue should be rejected");
    expect_true(!esp32_mquickjs_dma_completion_queue_init(
                    &queue, ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT + 1U),
                "completion queue cannot exceed fixed staging slots");
    expect_true(esp32_mquickjs_dma_completion_queue_init(&queue, 2),
                "two-slot completion queue should initialize");
    expect_true(esp32_mquickjs_dma_completion_queue_releasable(&queue),
                "empty queue should permit close/release");

    expect_true(esp32_mquickjs_dma_completion_queue_next_submit(
                    &queue, &index) && index == 0,
                "first submission should use slot zero");
    expect_true(esp32_mquickjs_dma_completion_queue_note_submitted(
                    &queue, index),
                "first submission should commit");
    expect_true(esp32_mquickjs_dma_completion_queue_next_submit(
                    &queue, &index) && index == 1,
                "second submission should use slot one");
    expect_true(esp32_mquickjs_dma_completion_queue_note_submitted(
                    &queue, index),
                "second submission should commit");
    expect_true(!esp32_mquickjs_dma_completion_queue_can_submit(&queue),
                "full completion queue should apply backpressure");
    expect_true(!esp32_mquickjs_dma_completion_queue_releasable(&queue),
                "close-pending must retain ownership while DMA is in flight");

    expect_true(esp32_mquickjs_dma_completion_queue_note_returned(
                    &queue, 1) ==
                    ESP32_MQUICKJS_DMA_COMPLETION_OUT_OF_ORDER,
                "out-of-order completion should be detected");
    expect_true(queue.in_flight == 1 && queue.occupied == 2,
                "returned later slot stays occupied behind earlier DMA");
    expect_true(!esp32_mquickjs_dma_completion_queue_can_submit(&queue),
                "out-of-order returned slot must not be reused early");
    expect_true(esp32_mquickjs_dma_completion_queue_peek_in_flight(
                    &queue, &index) && index == 0,
                "earliest outstanding slot remains the completion head");
    expect_true(!esp32_mquickjs_dma_completion_queue_releasable(&queue),
                "close-pending retains both slots until the head returns");

    expect_true(esp32_mquickjs_dma_completion_queue_note_returned(
                    &queue, 0) ==
                    ESP32_MQUICKJS_DMA_COMPLETION_IN_ORDER,
                "head completion should release the ordered prefix");
    expect_true(esp32_mquickjs_dma_completion_queue_releasable(&queue),
                "all returned DMA permits close/release");
    expect_true(esp32_mquickjs_dma_completion_queue_can_submit(&queue),
                "drained slots should be reusable");
    expect_true(esp32_mquickjs_dma_completion_queue_note_returned(
                    &queue, 0) ==
                    ESP32_MQUICKJS_DMA_COMPLETION_INVALID,
                "duplicate completion must be rejected");
}

static void test_cancellation_before_start_and_in_flight(void)
{
    esp32_mquickjs_dma_completion_queue_t queue;
    uint32_t index;

    expect_true(esp32_mquickjs_dma_completion_queue_init(&queue, 2),
                "cancellation queue should initialize");
    expect_true(esp32_mquickjs_dma_cancel_disposition(
                    &queue, false, false) ==
                    ESP32_MQUICKJS_DMA_CANCEL_COMPLETE,
                "cancel before start should complete immediately");
    expect_true(esp32_mquickjs_dma_completion_queue_next_submit(
                    &queue, &index) &&
                esp32_mquickjs_dma_completion_queue_note_submitted(
                    &queue, index),
                "in-flight cancellation fixture should submit DMA");
    expect_true(esp32_mquickjs_dma_cancel_disposition(
                    &queue, false, false) ==
                    ESP32_MQUICKJS_DMA_CANCEL_PENDING,
                "cancel in flight should wait for driver completion");
    expect_true(esp32_mquickjs_dma_cancel_disposition(
                    &queue, false, true) ==
                    ESP32_MQUICKJS_DMA_CANCEL_REJECTED,
                "duplicate cancellation request should be rejected");
    expect_true(!esp32_mquickjs_dma_completion_queue_releasable(&queue),
                "cancel request must retain DMA ownership");
    expect_true(esp32_mquickjs_dma_completion_queue_note_returned(
                    &queue, index) ==
                    ESP32_MQUICKJS_DMA_COMPLETION_IN_ORDER,
                "cancelled in-flight DMA should still be reaped");
    expect_true(esp32_mquickjs_dma_completion_queue_releasable(&queue),
                "reaped cancellation can release resources");
    expect_true(esp32_mquickjs_dma_cancel_disposition(
                    &queue, true, false) ==
                    ESP32_MQUICKJS_DMA_CANCEL_REJECTED,
                "completed transfer should reject late cancellation");
}

static void test_completion_wins_over_late_deadline_observation(void)
{
    expect_true(!esp32_mquickjs_dma_progress_timed_out(
                    99999, 100000, false),
                "pending transfer before deadline should keep waiting");
    expect_true(esp32_mquickjs_dma_progress_timed_out(
                    100000, 100000, false),
                "pending transfer at deadline should time out");
    expect_true(!esp32_mquickjs_dma_progress_timed_out(
                    150000, 100000, true),
                "completion reaped after a delayed scheduler turn must win");
    expect_true(!esp32_mquickjs_dma_progress_timed_out(
                    UINT64_MAX, 0, false),
                "disabled progress deadline should never time out");
}

int main(void)
{
    test_source_classifier();
    test_empty_odd_and_long_spans();
    test_slot_rotation_and_contention();
    test_workspace_allocation_failure_is_all_or_nothing();
    test_mixed_stats_and_progress_validation();
    test_completion_order_and_close_pending_ownership();
    test_cancellation_before_start_and_in_flight();
    test_completion_wins_over_late_deadline_observation();
    return 0;
}
