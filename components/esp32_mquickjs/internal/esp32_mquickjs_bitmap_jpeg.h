#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESP32_MQUICKJS_BITMAP_JPEG_OK = 0,
    ESP32_MQUICKJS_BITMAP_JPEG_INVALID,
    ESP32_MQUICKJS_BITMAP_JPEG_TRUNCATED,
    ESP32_MQUICKJS_BITMAP_JPEG_UNSUPPORTED,
} esp32_mquickjs_bitmap_jpeg_result_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t components;
} esp32_mquickjs_bitmap_jpeg_info_t;

esp32_mquickjs_bitmap_jpeg_result_t esp32_mquickjs_bitmap_jpeg_parse(
    const uint8_t *data,
    size_t length,
    esp32_mquickjs_bitmap_jpeg_info_t *out_info);
