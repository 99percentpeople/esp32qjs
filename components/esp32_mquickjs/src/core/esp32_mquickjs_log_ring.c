#include "esp32_mquickjs_log_ring.h"

#include <string.h>

void esp32_mquickjs_log_ring_init(esp32_mquickjs_log_ring_t *ring)
{
    if (ring == NULL) {
        return;
    }
    memset(ring, 0, sizeof(*ring));
    ring->next_sequence = 1U;
}

uint32_t esp32_mquickjs_log_ring_append(esp32_mquickjs_log_ring_t *ring,
                                        uint32_t uptime_ms,
                                        esp32_mquickjs_log_source_t source,
                                        const char *text,
                                        size_t text_length)
{
    esp32_mquickjs_log_entry_t *entry;
    size_t bounded_length;
    uint16_t index;

    if (ring == NULL || text == NULL || text_length == 0U) {
        return 0U;
    }
    bounded_length = text_length > ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES
                         ? ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES
                         : text_length;
    if (ring->count == ESP32_MQUICKJS_LOG_RING_CAPACITY) {
        index = ring->head;
        ring->head = (uint16_t)((ring->head + 1U) % ESP32_MQUICKJS_LOG_RING_CAPACITY);
        ring->dropped++;
    } else {
        index = (uint16_t)((ring->head + ring->count) % ESP32_MQUICKJS_LOG_RING_CAPACITY);
        ring->count++;
    }
    entry = &ring->entries[index];
    entry->sequence = ring->next_sequence++;
    if (ring->next_sequence == 0U) {
        ring->next_sequence = 1U;
    }
    entry->uptime_ms = uptime_ms;
    entry->source = source;
    entry->text_length = (uint16_t)bounded_length;
    memcpy(entry->text, text, bounded_length);
    entry->text[bounded_length] = '\0';
    return entry->sequence;
}

size_t esp32_mquickjs_log_ring_read(const esp32_mquickjs_log_ring_t *ring,
                                    uint32_t after_sequence,
                                    size_t limit,
                                    size_t max_text_bytes,
                                    esp32_mquickjs_log_entry_t *output,
                                    size_t output_capacity)
{
    size_t copied = 0U;
    size_t copied_bytes = 0U;
    size_t i;

    if (ring == NULL || output == NULL || limit == 0U || output_capacity == 0U) {
        return 0U;
    }
    if (limit > output_capacity) {
        limit = output_capacity;
    }
    for (i = 0U; i < ring->count && copied < limit; ++i) {
        const esp32_mquickjs_log_entry_t *entry =
            &ring->entries[(ring->head + i) % ESP32_MQUICKJS_LOG_RING_CAPACITY];

        if (entry->sequence <= after_sequence) {
            continue;
        }
        if (copied > 0U && copied_bytes + entry->text_length > max_text_bytes) {
            break;
        }
        if (copied == 0U && entry->text_length > max_text_bytes) {
            continue;
        }
        output[copied++] = *entry;
        copied_bytes += entry->text_length;
    }
    return copied;
}
