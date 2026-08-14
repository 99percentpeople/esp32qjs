#pragma once

#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_LOG_RING_CAPACITY 64U
#define ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES 512U

typedef enum {
    ESP32_MQUICKJS_LOG_SOURCE_JAVASCRIPT = 0,
    ESP32_MQUICKJS_LOG_SOURCE_EXCEPTION,
    ESP32_MQUICKJS_LOG_SOURCE_RUNTIME,
    ESP32_MQUICKJS_LOG_SOURCE_ESP_IDF,
} esp32_mquickjs_log_source_t;

typedef struct {
    uint32_t sequence;
    uint32_t uptime_ms;
    esp32_mquickjs_log_source_t source;
    uint16_t text_length;
    char text[ESP32_MQUICKJS_LOG_TEXT_MAX_BYTES + 1U];
} esp32_mquickjs_log_entry_t;

typedef struct {
    esp32_mquickjs_log_entry_t entries[ESP32_MQUICKJS_LOG_RING_CAPACITY];
    uint32_t next_sequence;
    uint32_t dropped;
    uint16_t head;
    uint16_t count;
} esp32_mquickjs_log_ring_t;

void esp32_mquickjs_log_ring_init(esp32_mquickjs_log_ring_t *ring);

uint32_t esp32_mquickjs_log_ring_append(esp32_mquickjs_log_ring_t *ring,
                                        uint32_t uptime_ms,
                                        esp32_mquickjs_log_source_t source,
                                        const char *text,
                                        size_t text_length);

size_t esp32_mquickjs_log_ring_read(const esp32_mquickjs_log_ring_t *ring,
                                    uint32_t after_sequence,
                                    size_t limit,
                                    size_t max_text_bytes,
                                    esp32_mquickjs_log_entry_t *output,
                                    size_t output_capacity);
