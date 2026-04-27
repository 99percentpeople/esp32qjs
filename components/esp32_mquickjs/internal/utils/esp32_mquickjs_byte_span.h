#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mquickjs.h"

typedef struct {
    const uint8_t *data;
    size_t length;
    JSValue owner;
    bool dma_capable;
} esp32_mquickjs_byte_span_t;

typedef struct {
    void *opaque;
    bool (*next)(JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out);
    void (*close)(JSContext *ctx, void *opaque);
} esp32_mquickjs_byte_span_source_t;

void esp32_mquickjs_byte_span_clear(esp32_mquickjs_byte_span_t *span);
bool esp32_mquickjs_byte_span_source_next(JSContext *ctx,
                                          esp32_mquickjs_byte_span_source_t *source,
                                          esp32_mquickjs_byte_span_t *out);
void esp32_mquickjs_byte_span_source_close(JSContext *ctx,
                                           esp32_mquickjs_byte_span_source_t *source);
