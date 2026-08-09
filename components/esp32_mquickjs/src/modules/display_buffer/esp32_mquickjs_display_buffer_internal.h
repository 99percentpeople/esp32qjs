#pragma once

#include "esp32_mquickjs_display_buffer.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

#include "esp32_mquickjs_core.h"
#include "utils/esp32_mquickjs_font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DISPLAY_BUFFER_MAX_DIMENSION 4096U
#define DISPLAY_BUFFER_DEFAULT_CHUNK_BYTES 4096U

typedef enum {
    DISPLAY_BUFFER_FORMAT_MONO1 = 1,
    DISPLAY_BUFFER_FORMAT_RGB565 = 2,
} display_buffer_format_t;

typedef enum {
    DISPLAY_BUFFER_LAYOUT_LINEAR = 1,
    DISPLAY_BUFFER_LAYOUT_PAGE_Y8 = 2,
} display_buffer_layout_t;

typedef enum {
    DISPLAY_BUFFER_STORAGE_AUTO = 1,
    DISPLAY_BUFFER_STORAGE_INTERNAL = 2,
    DISPLAY_BUFFER_STORAGE_PSRAM = 3,
    DISPLAY_BUFFER_STORAGE_DMA = 4,
} display_buffer_storage_t;

struct esp32_mquickjs_display_buffer {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t page_height;
    uint8_t format;
    uint8_t layout;
    uint8_t storage;
    uint8_t closed;
    size_t byte_length;
    uint8_t *data;
    uint8_t *chunk;
    size_t chunk_size;
    size_t chunk_capacity;
    uint16_t foreground;
    uint16_t background;
    int dirty_x0;
    int dirty_y0;
    int dirty_x1;
    int dirty_y1;
};

typedef struct {
    esp32_mquickjs_bitmap_font_t font;
    uint8_t *glyphs;
    char *name;
} esp32_mquickjs_display_font_t;

bool value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value);
bool value_to_i32(JSContext *ctx, JSValue value, int32_t *out_value);
uint16_t normalize_color(JSContext *ctx, uint8_t format, JSValue value, uint16_t fallback, bool *ok);

esp32_mquickjs_display_buffer_t *display_buffer_from_value(JSContext *ctx,
                                                           JSValue value,
                                                           const char *api_name);
void mark_dirty(esp32_mquickjs_display_buffer_t *buffer, int x, int y, int width, int height);
void set_pixel_raw(esp32_mquickjs_display_buffer_t *buffer, int x, int y, uint16_t color);
void fill_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
                   int x,
                   int y,
                   int width,
                   int height,
                   uint16_t color,
                   bool update_dirty);
void draw_line_raw(esp32_mquickjs_display_buffer_t *buffer,
                   int x0,
                   int y0,
                   int x1,
                   int y1,
                   uint16_t color);
void draw_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
                   int32_t x,
                   int32_t y,
                   int32_t width,
                   int32_t height,
                   uint16_t color);
void draw_round_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
                         int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         uint32_t radius,
                         uint16_t color);
void fill_round_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
                         int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         uint32_t radius,
                         uint16_t color);
void display_buffer_draw_text_raw(esp32_mquickjs_display_buffer_t *buffer,
                                  int32_t x,
                                  int32_t y,
                                  const char *text,
                                  const esp32_mquickjs_bitmap_font_t *font,
                                  int spacing,
                                  uint16_t color,
                                  bool has_background,
                                  uint16_t background);
bool rect_from_args(JSContext *ctx,
                    int argc,
                    JSValue *argv,
                    int32_t *x,
                    int32_t *y,
                    int32_t *width,
                    int32_t *height,
                    const char *api_name);

esp32_mquickjs_display_font_t *display_font_from_value(JSContext *ctx,
                                                       JSValue value,
                                                       const char *api_name);

#endif
