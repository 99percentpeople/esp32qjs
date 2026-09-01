#include "esp32_mquickjs_bitmap_jpeg.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BITMAP_JPEG_MAX_DIMENSION 4096U

static bool marker_is_standalone(uint8_t marker)
{
    return marker == 0x01U || marker == 0xd8U || marker == 0xd9U ||
           (marker >= 0xd0U && marker <= 0xd7U);
}

static bool marker_is_sof(uint8_t marker)
{
    return marker >= 0xc0U && marker <= 0xcfU && marker != 0xc4U &&
           marker != 0xc8U && marker != 0xccU;
}

esp32_mquickjs_bitmap_jpeg_result_t esp32_mquickjs_bitmap_jpeg_parse(
    const uint8_t *data,
    size_t length,
    esp32_mquickjs_bitmap_jpeg_info_t *out_info)
{
    size_t offset = 2U;

    if (data == NULL || out_info == NULL || length < 4U || data[0] != 0xffU ||
        data[1] != 0xd8U) {
        return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
    }
    out_info->width = 0;
    out_info->height = 0;
    out_info->components = 0;

    while (offset < length) {
        uint8_t marker;
        uint16_t segment_length;

        if (data[offset] != 0xffU) {
            return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
        }
        while (offset < length && data[offset] == 0xffU) {
            ++offset;
        }
        if (offset >= length) {
            return ESP32_MQUICKJS_BITMAP_JPEG_TRUNCATED;
        }
        marker = data[offset++];
        if (marker == 0x00U) {
            return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
        }
        if (marker_is_standalone(marker)) {
            if (marker == 0xd9U) {
                return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
            }
            continue;
        }
        if (offset + 2U > length) {
            return ESP32_MQUICKJS_BITMAP_JPEG_TRUNCATED;
        }
        segment_length = ((uint16_t)data[offset] << 8U) | data[offset + 1U];
        if (segment_length < 2U) {
            return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
        }
        if ((size_t)segment_length > length - offset) {
            return ESP32_MQUICKJS_BITMAP_JPEG_TRUNCATED;
        }
        if (marker_is_sof(marker)) {
            uint16_t height;
            uint16_t width;
            uint8_t components;

            if (marker != 0xc0U) {
                return ESP32_MQUICKJS_BITMAP_JPEG_UNSUPPORTED;
            }
            if (segment_length < 8U) {
                return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
            }
            height = ((uint16_t)data[offset + 3U] << 8U) |
                     data[offset + 4U];
            width = ((uint16_t)data[offset + 5U] << 8U) |
                    data[offset + 6U];
            components = data[offset + 7U];
            if (data[offset + 2U] != 8U || width == 0U || height == 0U ||
                width > BITMAP_JPEG_MAX_DIMENSION ||
                height > BITMAP_JPEG_MAX_DIMENSION ||
                (components != 1U && components != 3U) ||
                segment_length != (uint16_t)(8U + 3U * components)) {
                return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
            }
            out_info->width = width;
            out_info->height = height;
            out_info->components = components;
            return ESP32_MQUICKJS_BITMAP_JPEG_OK;
        }
        if (marker == 0xdaU) {
            return ESP32_MQUICKJS_BITMAP_JPEG_INVALID;
        }
        offset += segment_length;
    }
    return ESP32_MQUICKJS_BITMAP_JPEG_TRUNCATED;
}
