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

    expect_size(
        (size_t)esp32_mquickjs_dma_progress_timeout_us(1, 80000000),
        100000,
        "small transfers should use the 100 ms progress floor");
    expect_size(
        (size_t)esp32_mquickjs_dma_progress_timeout_us(1000000, 1000000),
        32050000,
        "large transfers should use four wire times plus 50 ms");
}

int main(void)
{
    test_source_classifier();
    test_empty_odd_and_long_spans();
    test_slot_rotation_and_contention();
    test_mixed_stats_and_progress_validation();
    return 0;
}
