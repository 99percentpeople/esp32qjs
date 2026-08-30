#pragma once

#include "esp32_mquickjs_bitmap.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_bitmap_image.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP

#include "esp32_mquickjs_core.h"
#include "utils/esp32_mquickjs_font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BITMAP_MAX_DIMENSION 4096U
#define BITMAP_DEFAULT_CHUNK_BYTES 4096U

#define BITMAP_FORMAT_MONO1 ESP32_MQUICKJS_BITMAP_FORMAT_MONO1
#define BITMAP_FORMAT_GRAY8 ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8
#define BITMAP_FORMAT_RGB565 ESP32_MQUICKJS_BITMAP_FORMAT_RGB565
#define BITMAP_FORMAT_RGB888 ESP32_MQUICKJS_BITMAP_FORMAT_RGB888

#define BITMAP_LAYOUT_LINEAR ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR
#define BITMAP_LAYOUT_PAGE_Y8 ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8

typedef enum {
    BITMAP_STORAGE_AUTO = 1,
    BITMAP_STORAGE_INTERNAL = 2,
    BITMAP_STORAGE_PSRAM = 3,
    BITMAP_STORAGE_DMA = 4,
} bitmap_storage_t;

struct esp32_mquickjs_bitmap {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t page_height;
    uint8_t format;
    uint8_t layout;
    uint8_t storage;
    uint8_t closed;
    uint16_t read_leases;
    bool write_lease;
    size_t byte_length;
    uint8_t *data;
    size_t chunk_size;
    uint32_t foreground;
    uint32_t background;
    int dirty_x0;
    int dirty_y0;
    int dirty_x1;
    int dirty_y1;
};

typedef struct {
    esp32_mquickjs_bitmap_font_t font;
    uint8_t *glyphs;
    esp32_mquickjs_memory_block_t *glyphs_block;
    char *name;
} esp32_mquickjs_display_font_t;

bool value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value);
bool value_to_i32(JSContext *ctx, JSValue value, int32_t *out_value);
uint32_t normalize_color(JSContext *ctx, uint8_t format, JSValue value, uint32_t fallback, bool *ok);

esp32_mquickjs_bitmap_t *bitmap_from_value(JSContext *ctx,
                                                           JSValue value,
                                                           const char *api_name);
bool bitmap_require_readable(JSContext *ctx,
                             esp32_mquickjs_bitmap_t *buffer,
                             const char *api_name);
bool bitmap_require_writable(JSContext *ctx,
                             esp32_mquickjs_bitmap_t *buffer,
                             const char *api_name);
bool bitmap_acquire_read(JSContext *ctx,
                         esp32_mquickjs_bitmap_t *buffer,
                         const char *api_name);
void bitmap_release_read(esp32_mquickjs_bitmap_t *buffer);
bool bitmap_acquire_write(JSContext *ctx,
                          esp32_mquickjs_bitmap_t *buffer,
                          const char *api_name);
void bitmap_release_write(esp32_mquickjs_bitmap_t *buffer);
esp32_mquickjs_bitmap_t *bitmap_allocate(JSContext *ctx,
                                         uint32_t width,
                                         uint32_t height,
                                         uint8_t format,
                                         uint8_t layout,
                                         uint8_t storage,
                                         uint32_t requested_stride,
                                         uint32_t chunk_bytes,
                                         uint32_t foreground,
                                         uint32_t background);
void bitmap_free(esp32_mquickjs_bitmap_t *buffer);
JSValue bitmap_wrap(JSContext *ctx, esp32_mquickjs_bitmap_t *buffer);
void mark_dirty(esp32_mquickjs_bitmap_t *buffer, int x, int y, int width, int height);
void set_pixel_raw(esp32_mquickjs_bitmap_t *buffer, int x, int y, uint32_t color);
void fill_rect_raw(esp32_mquickjs_bitmap_t *buffer,
                   int x,
                   int y,
                   int width,
                   int height,
                   uint32_t color,
                   bool update_dirty);
void draw_line_raw(esp32_mquickjs_bitmap_t *buffer,
                   int x0,
                   int y0,
                   int x1,
                   int y1,
                   uint32_t color);
void draw_rect_raw(esp32_mquickjs_bitmap_t *buffer,
                   int32_t x,
                   int32_t y,
                   int32_t width,
                   int32_t height,
                   uint32_t color);
void draw_round_rect_raw(esp32_mquickjs_bitmap_t *buffer,
                         int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         uint32_t radius,
                         uint32_t color);
void fill_round_rect_raw(esp32_mquickjs_bitmap_t *buffer,
                         int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height,
                         uint32_t radius,
                         uint32_t color);
void bitmap_draw_text_raw(esp32_mquickjs_bitmap_t *buffer,
                                  int32_t x,
                                  int32_t y,
                                  const char *text,
                                  const esp32_mquickjs_bitmap_font_t *font,
                                  int spacing,
                                  uint32_t color,
                                  bool has_background,
                                  uint32_t background);
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
