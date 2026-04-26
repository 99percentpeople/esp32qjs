#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp32_mquickjs_types.h"

typedef struct {
    const uint8_t *data;
    size_t length;
    JSValue owner;
} esp32_mquickjs_byte_source_t;

typedef struct {
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned;
    JSGCRef value_ref;
    bool rooted;
} esp32_mquickjs_byte_source_chunk_t;

bool esp32_mquickjs_get_byte_source(JSContext *ctx,
                                    JSValue value,
                                    const char *api_name,
                                    esp32_mquickjs_byte_source_t *out,
                                    uint8_t **out_owned,
                                    JSValue *out_error);

bool esp32_mquickjs_get_byte_source_array_length(JSContext *ctx,
                                                 JSValue value,
                                                 const char *api_name,
                                                 uint32_t *out_length,
                                                 JSValue *out_error);

bool esp32_mquickjs_get_byte_source_chunk(JSContext *ctx,
                                          JSValue chunks,
                                          uint32_t index,
                                          const char *api_name,
                                          esp32_mquickjs_byte_source_chunk_t *out,
                                          JSValue *out_error);

void esp32_mquickjs_release_byte_source_chunk(JSContext *ctx,
                                              esp32_mquickjs_byte_source_chunk_t *chunk);

JSValue esp32_mquickjs_new_byte_view(JSContext *ctx,
                                     JSValue owner,
                                     const uint8_t *data,
                                     size_t length);

JSValue esp32_mquickjs_new_owned_byte_view(JSContext *ctx,
                                           uint8_t *data,
                                           size_t length);

bool esp32_mquickjs_update_byte_view(JSContext *ctx,
                                     JSValue value,
                                     const uint8_t *data,
                                     size_t length);

void esp32_mquickjs_release_byte_source(uint8_t *owned);

bool esp32_mquickjs_byte_source_take_gc_request(void);

JSValue js_byte_view_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_byte_view_finalizer(JSContext *ctx, void *opaque);
JSValue js_byte_view_get_length(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_byte_view_to_array(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
