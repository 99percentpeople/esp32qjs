#include "esp32_mquickjs_log_ring.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_append_and_read_in_sequence_order(void)
{
    esp32_mquickjs_log_ring_t ring;
    esp32_mquickjs_log_entry_t output[4];
    size_t count;

    esp32_mquickjs_log_ring_init(&ring);
    assert(esp32_mquickjs_log_ring_append(
               &ring, 10, ESP32_MQUICKJS_LOG_SOURCE_JAVASCRIPT,
               "one\n", 4) == 1);
    assert(esp32_mquickjs_log_ring_append(
               &ring, 20, ESP32_MQUICKJS_LOG_SOURCE_EXCEPTION,
               "two", 3) == 2);

    count = esp32_mquickjs_log_ring_read(&ring, 0, 4, 32, output, 4);
    assert(count == 2);
    assert(output[0].sequence == 1);
    assert(output[0].text_length == 4);
    assert(strcmp(output[0].text, "one\n") == 0);
    assert(output[1].sequence == 2);
    assert(output[1].source == ESP32_MQUICKJS_LOG_SOURCE_EXCEPTION);

    count = esp32_mquickjs_log_ring_read(&ring, 1, 4, 32, output, 4);
    assert(count == 1);
    assert(output[0].sequence == 2);
}

static void test_overwrite_counts_dropped_entries(void)
{
    esp32_mquickjs_log_ring_t ring;
    esp32_mquickjs_log_entry_t output[ESP32_MQUICKJS_LOG_RING_CAPACITY];
    size_t i;
    size_t count;

    esp32_mquickjs_log_ring_init(&ring);
    for (i = 0; i < ESP32_MQUICKJS_LOG_RING_CAPACITY + 3U; ++i) {
        char text[24];
        int length = snprintf(text, sizeof(text), "entry-%u", (unsigned)i);

        assert(length > 0);
        (void)esp32_mquickjs_log_ring_append(
            &ring, (uint32_t)i, ESP32_MQUICKJS_LOG_SOURCE_ESP_IDF,
            text, (size_t)length);
    }
    assert(ring.dropped == 3);
    count = esp32_mquickjs_log_ring_read(
        &ring, 0, ESP32_MQUICKJS_LOG_RING_CAPACITY, 65536,
        output, ESP32_MQUICKJS_LOG_RING_CAPACITY);
    assert(count == ESP32_MQUICKJS_LOG_RING_CAPACITY);
    assert(output[0].sequence == 4);
    assert(output[count - 1U].sequence == ESP32_MQUICKJS_LOG_RING_CAPACITY + 3U);
}

static void test_read_honors_text_budget_without_splitting_entries(void)
{
    esp32_mquickjs_log_ring_t ring;
    esp32_mquickjs_log_entry_t output[3];
    size_t count;

    esp32_mquickjs_log_ring_init(&ring);
    (void)esp32_mquickjs_log_ring_append(
        &ring, 1, ESP32_MQUICKJS_LOG_SOURCE_JAVASCRIPT, "1234", 4);
    (void)esp32_mquickjs_log_ring_append(
        &ring, 2, ESP32_MQUICKJS_LOG_SOURCE_JAVASCRIPT, "5678", 4);
    count = esp32_mquickjs_log_ring_read(&ring, 0, 3, 7, output, 3);
    assert(count == 1);
    assert(strcmp(output[0].text, "1234") == 0);
}

int main(void)
{
    test_append_and_read_in_sequence_order();
    test_overwrite_counts_dropped_entries();
    test_read_honors_text_budget_without_splitting_entries();
    return 0;
}
