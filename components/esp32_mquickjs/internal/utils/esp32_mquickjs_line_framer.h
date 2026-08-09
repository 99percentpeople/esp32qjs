#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*esp32_mquickjs_line_framer_emit_fn)(void *opaque,
                                                    const char *frame,
                                                    size_t frame_len,
                                                    bool overflow);

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool dropping;
    bool swallow_lf;
} esp32_mquickjs_line_framer_t;

void esp32_mquickjs_line_framer_init(esp32_mquickjs_line_framer_t *framer,
                                     char *buffer,
                                     size_t capacity);

size_t esp32_mquickjs_line_framer_feed(esp32_mquickjs_line_framer_t *framer,
                                       const uint8_t *data,
                                       size_t data_len,
                                       esp32_mquickjs_line_framer_emit_fn emit,
                                       void *opaque);
