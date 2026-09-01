#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp32_mquickjs_types.h"
#include "utils/esp32_mquickjs_byte_span.h"

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
    bool read_leased;
} esp32_mquickjs_byte_source_chunk_t;

typedef void (*esp32_mquickjs_byte_view_release_fn)(void *opaque);

typedef struct {
    int class_id;
    bool (*open)(JSContext *ctx,
                 JSValue source_value,
                 void *opaque,
                 esp32_mquickjs_byte_span_source_t *out,
                 JSValue *out_error);
    bool (*set_rect)(JSContext *ctx,
                     void *opaque,
                     int32_t x,
                     int32_t y,
                     int32_t width,
                     int32_t height);
    size_t (*known_length)(void *opaque);
    void (*destroy)(JSContext *ctx, void *opaque);
} esp32_mquickjs_byte_span_source_object_ops_t;

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

bool esp32_mquickjs_open_byte_span_source(JSContext *ctx,
                                          JSValue value,
                                          const char *api_name,
                                          esp32_mquickjs_byte_span_source_t *out,
                                          JSValue *out_error);

bool esp32_mquickjs_byte_span_source_known_length(JSContext *ctx,
                                                  JSValue value,
                                                  size_t *out_length);

void *esp32_mquickjs_byte_span_source_get_opaque(
    JSContext *ctx,
    JSValue value,
    const esp32_mquickjs_byte_span_source_object_ops_t *expected_ops,
    const char *api_name);

JSValue esp32_mquickjs_new_byte_span_source(JSContext *ctx,
                                            JSValue owner,
                                            const esp32_mquickjs_byte_span_source_object_ops_t *ops,
                                            void *opaque);

JSValue esp32_mquickjs_new_owned_byte_view(JSContext *ctx,
                                           uint8_t *data,
                                           size_t length);

JSValue esp32_mquickjs_new_retained_byte_view(
    JSContext *ctx,
    const uint8_t *data,
    size_t length,
    esp32_mquickjs_byte_view_release_fn release,
    void *release_opaque);

bool esp32_mquickjs_byte_view_is_open(JSContext *ctx, JSValue value);

bool esp32_mquickjs_byte_view_acquire_read(JSContext *ctx,
                                           JSValue value,
                                           const char *api_name,
                                           const uint8_t **out_data,
                                           size_t *out_length);
void esp32_mquickjs_byte_view_release_read(JSContext *ctx, JSValue value);

void esp32_mquickjs_release_byte_source(uint8_t *owned);

JSValue js_byte_view_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_byte_view_finalizer(JSContext *ctx, void *opaque);
JSValue js_byte_view_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_byte_view_get_length(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_byte_view_get_uint8(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_byte_view_to_array(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_byte_span_source_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_byte_span_source_get_length(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_byte_span_source_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_span_source_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_byte_span_source_finalizer(JSContext *ctx, void *opaque);
JSValue js_bitmap_span_source_set_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
