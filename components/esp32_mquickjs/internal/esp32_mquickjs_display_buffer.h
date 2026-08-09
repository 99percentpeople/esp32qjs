#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

typedef struct esp32_mquickjs_display_buffer esp32_mquickjs_display_buffer_t;

typedef enum {
    ESP32_MQUICKJS_DISPLAY_BUFFER_BYTE_ORDER_BE = 0,
    ESP32_MQUICKJS_DISPLAY_BUFFER_BYTE_ORDER_LE = 1,
} esp32_mquickjs_display_buffer_byte_order_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    esp32_mquickjs_display_buffer_byte_order_t byte_order;
} esp32_mquickjs_display_buffer_rect_t;

JSValue js_display_buffer_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_display_buffer_finalizer(JSContext *ctx, void *opaque);
JSValue js_display_font_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_display_font_finalizer(JSContext *ctx, void *opaque);
JSValue js_display_command_buffer_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_display_command_buffer_finalizer(JSContext *ctx, void *opaque);

esp32_mquickjs_display_buffer_t *esp32_mquickjs_display_buffer_from_value(JSContext *ctx,
                                                                          JSValue value,
                                                                          const char *api_name);
bool esp32_mquickjs_display_buffer_normalize_rect(const esp32_mquickjs_display_buffer_t *buffer,
                                                  esp32_mquickjs_display_buffer_rect_t *rect);
size_t esp32_mquickjs_display_buffer_rect_length(const esp32_mquickjs_display_buffer_t *buffer,
                                                 int32_t width,
                                                 int32_t height);
size_t esp32_mquickjs_display_buffer_row_length(const esp32_mquickjs_display_buffer_t *buffer,
                                                int32_t width);
uint32_t esp32_mquickjs_display_buffer_chunk_bytes(const esp32_mquickjs_display_buffer_t *buffer);
bool esp32_mquickjs_display_buffer_direct_rect(const esp32_mquickjs_display_buffer_t *buffer,
                                               const esp32_mquickjs_display_buffer_rect_t *rect,
                                               const uint8_t **out_data,
                                               size_t *out_length);
bool esp32_mquickjs_display_buffer_export_rect(const esp32_mquickjs_display_buffer_t *buffer,
                                               const esp32_mquickjs_display_buffer_rect_t *rect,
                                               uint8_t *out,
                                               size_t out_length);

JSValue js_display_buffer_create(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_load_font(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_display_font_get_name(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_advance(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_line_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_display_buffer_get_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_format(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_layout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_stride(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_page_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_byte_length(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_display_buffer_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_set_pixel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_pixel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_fill_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_circle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_fill_circle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_ellipse(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_fill_ellipse(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_fill_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_polyline(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_polygon(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_fill_polygon(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_triangle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_fill_triangle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_quadratic_bezier(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_cubic_bezier(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_bitmap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_measure_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_clear_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_mark_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_read_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_read_rect_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_create_span_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_create_command_buffer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_display_command_buffer_reset(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_fill_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_draw_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_draw_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_draw_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_fill_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_draw_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_append_packed(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_replay(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_command_buffer_stats(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
