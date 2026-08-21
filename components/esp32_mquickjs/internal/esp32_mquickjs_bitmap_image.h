#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESP32_MQUICKJS_BITMAP_FORMAT_MONO1 = 1,
    ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8 = 2,
    ESP32_MQUICKJS_BITMAP_FORMAT_RGB565 = 3,
    ESP32_MQUICKJS_BITMAP_FORMAT_RGB888 = 4,
} esp32_mquickjs_bitmap_pixel_format_t;

typedef enum {
    ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR = 1,
    ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8 = 2,
} esp32_mquickjs_bitmap_layout_t;

typedef enum {
    ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE = 0,
    ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE = 1,
} esp32_mquickjs_bitmap_byte_order_t;

typedef enum {
    ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB = 0,
    ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB = 1,
} esp32_mquickjs_bitmap_bit_order_t;

typedef enum {
    ESP32_MQUICKJS_BITMAP_FILTER_NEAREST = 0,
    ESP32_MQUICKJS_BITMAP_FILTER_BILINEAR = 1,
} esp32_mquickjs_bitmap_filter_t;

typedef enum {
    ESP32_MQUICKJS_BITMAP_DITHER_NONE = 0,
    ESP32_MQUICKJS_BITMAP_DITHER_BAYER_4X4 = 1,
} esp32_mquickjs_bitmap_dither_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    esp32_mquickjs_bitmap_pixel_format_t format;
    esp32_mquickjs_bitmap_layout_t layout;
    esp32_mquickjs_bitmap_byte_order_t byte_order;
    esp32_mquickjs_bitmap_bit_order_t bit_order;
} esp32_mquickjs_bitmap_view_t;

typedef struct {
    uint8_t *data;
    size_t length;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    esp32_mquickjs_bitmap_pixel_format_t format;
    esp32_mquickjs_bitmap_layout_t layout;
    esp32_mquickjs_bitmap_byte_order_t byte_order;
    esp32_mquickjs_bitmap_bit_order_t bit_order;
} esp32_mquickjs_bitmap_target_t;

typedef struct {
    uint32_t source_x;
    uint32_t source_y;
    uint32_t source_width;
    uint32_t source_height;
    int32_t destination_x;
    int32_t destination_y;
    uint32_t destination_width;
    uint32_t destination_height;
    uint16_t rotation;
    bool flip_x;
    bool flip_y;
    esp32_mquickjs_bitmap_filter_t filter;
    esp32_mquickjs_bitmap_dither_t dither;
    bool normalize;
    uint8_t threshold;
} esp32_mquickjs_bitmap_transform_options_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} esp32_mquickjs_bitmap_dirty_rect_t;

typedef bool (*esp32_mquickjs_bitmap_cancel_fn_t)(void *opaque);

typedef enum {
    ESP32_MQUICKJS_BITMAP_TRANSFORM_OK = 0,
    ESP32_MQUICKJS_BITMAP_TRANSFORM_CANCELLED = 1,
    ESP32_MQUICKJS_BITMAP_TRANSFORM_INVALID = 2,
} esp32_mquickjs_bitmap_transform_result_t;

bool esp32_mquickjs_bitmap_compute_storage(
    uint32_t width,
    uint32_t height,
    esp32_mquickjs_bitmap_pixel_format_t format,
    esp32_mquickjs_bitmap_layout_t layout,
    uint32_t requested_stride,
    uint32_t *out_stride,
    size_t *out_length);

esp32_mquickjs_bitmap_transform_result_t esp32_mquickjs_bitmap_transform(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_target_t *target,
    const esp32_mquickjs_bitmap_transform_options_t *options,
    esp32_mquickjs_bitmap_cancel_fn_t cancel,
    void *cancel_opaque,
    uint32_t *out_rows_completed,
    esp32_mquickjs_bitmap_dirty_rect_t *out_dirty);
