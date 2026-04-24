#pragma once

#include "esp32_mquickjs_types.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

JSValue js_display_buffer_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_display_buffer_finalizer(JSContext *ctx, void *opaque);
JSValue js_display_font_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
void js_display_font_finalizer(JSContext *ctx, void *opaque);

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
JSValue js_display_buffer_draw_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_bitmap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_draw_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_measure_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_get_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_clear_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_mark_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_read_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_display_buffer_read_rect_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);

#endif
