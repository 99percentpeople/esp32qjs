#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp32_mquickjs_bitmap_image.h"
#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP

typedef struct esp32_mquickjs_bitmap esp32_mquickjs_bitmap_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    esp32_mquickjs_bitmap_byte_order_t byte_order;
} esp32_mquickjs_bitmap_rect_t;

bool esp32_mquickjs_init_bitmap_runtime(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_deinit_bitmap_runtime(JSContext *ctx);

JSValue js_bitmap_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_bitmap_finalizer(JSContext *ctx, void *opaque);
JSValue js_display_font_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_display_font_finalizer(JSContext *ctx, void *opaque);
JSValue js_display_command_buffer_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_display_command_buffer_finalizer(JSContext *ctx, void *opaque);

esp32_mquickjs_bitmap_t *esp32_mquickjs_bitmap_from_value(JSContext *ctx,
                                                                          JSValue value,
                                                                          const char *api_name);
bool esp32_mquickjs_bitmap_normalize_rect(const esp32_mquickjs_bitmap_t *buffer,
                                                  esp32_mquickjs_bitmap_rect_t *rect);
size_t esp32_mquickjs_bitmap_rect_length(const esp32_mquickjs_bitmap_t *buffer,
                                                 int32_t width,
                                                 int32_t height);
size_t esp32_mquickjs_bitmap_row_length(const esp32_mquickjs_bitmap_t *buffer,
                                                int32_t width);
uint32_t esp32_mquickjs_bitmap_chunk_bytes(const esp32_mquickjs_bitmap_t *buffer);
bool esp32_mquickjs_bitmap_direct_rect(const esp32_mquickjs_bitmap_t *buffer,
                                               const esp32_mquickjs_bitmap_rect_t *rect,
                                               const uint8_t **out_data,
                                               size_t *out_length);
bool esp32_mquickjs_bitmap_export_rect(const esp32_mquickjs_bitmap_t *buffer,
                                               const esp32_mquickjs_bitmap_rect_t *rect,
                                               uint8_t *out,
                                               size_t out_length);

JSValue js_bitmap_create(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_convert(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_load_font(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_display_font_get_name(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_advance(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_font_get_line_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_bitmap_get_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_format(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_layout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_stride(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_page_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_byte_length(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

JSValue js_bitmap_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_set_pixel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_pixel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_fill_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_circle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_fill_circle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_ellipse(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_fill_ellipse(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_fill_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_polyline(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_polygon(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_fill_polygon(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_triangle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_fill_triangle(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_quadratic_bezier(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_cubic_bezier(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_mask(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_blit(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_blit_batch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_draw_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_measure_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_get_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_clear_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_mark_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_read_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_read_rect_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_create_span_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_bitmap_create_command_buffer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

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
