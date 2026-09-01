#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp32_mquickjs_bitmap_jpeg.h"

static void test_baseline_dimensions(void)
{
    static const uint8_t jpeg[] = {
        0xff, 0xd8,
        0xff, 0xe0, 0x00, 0x04, 0x00, 0x00,
        0xff, 0xc0, 0x00, 0x0b, 0x08,
        0x00, 0x60, 0x00, 0x60, 0x01, 0x01, 0x11, 0x00,
        0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00,
    };
    esp32_mquickjs_bitmap_jpeg_info_t info = {0};

    assert(esp32_mquickjs_bitmap_jpeg_parse(jpeg, sizeof(jpeg), &info) ==
           ESP32_MQUICKJS_BITMAP_JPEG_OK);
    assert(info.width == 96);
    assert(info.height == 96);
    assert(info.components == 1);
}

static void test_rejects_progressive_and_truncated_inputs(void)
{
    static const uint8_t progressive[] = {
        0xff, 0xd8, 0xff, 0xc2, 0x00, 0x0b, 0x08,
        0x00, 0x10, 0x00, 0x20, 0x01, 0x01, 0x11, 0x00,
    };
    static const uint8_t truncated[] = {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x20, 0x00,
    };
    static const uint8_t not_jpeg[] = {0x89, 0x50, 0x4e, 0x47};
    esp32_mquickjs_bitmap_jpeg_info_t info = {0};

    assert(esp32_mquickjs_bitmap_jpeg_parse(
               progressive, sizeof(progressive), &info) ==
           ESP32_MQUICKJS_BITMAP_JPEG_UNSUPPORTED);
    assert(esp32_mquickjs_bitmap_jpeg_parse(
               truncated, sizeof(truncated), &info) ==
           ESP32_MQUICKJS_BITMAP_JPEG_TRUNCATED);
    assert(esp32_mquickjs_bitmap_jpeg_parse(
               not_jpeg, sizeof(not_jpeg), &info) ==
           ESP32_MQUICKJS_BITMAP_JPEG_INVALID);
}

static void test_rejects_invalid_dimensions_and_segment_lengths(void)
{
    static const uint8_t zero_width[] = {
        0xff, 0xd8, 0xff, 0xc0, 0x00, 0x0b, 0x08,
        0x00, 0x10, 0x00, 0x00, 0x01, 0x01, 0x11, 0x00,
    };
    static const uint8_t short_sof[] = {
        0xff, 0xd8, 0xff, 0xc0, 0x00, 0x07, 0x08,
        0x00, 0x10, 0x00, 0x10,
    };
    esp32_mquickjs_bitmap_jpeg_info_t info = {0};

    assert(esp32_mquickjs_bitmap_jpeg_parse(
               zero_width, sizeof(zero_width), &info) ==
           ESP32_MQUICKJS_BITMAP_JPEG_INVALID);
    assert(esp32_mquickjs_bitmap_jpeg_parse(
               short_sof, sizeof(short_sof), &info) ==
           ESP32_MQUICKJS_BITMAP_JPEG_INVALID);
}

int main(void)
{
    test_baseline_dimensions();
    test_rejects_progressive_and_truncated_inputs();
    test_rejects_invalid_dimensions_and_segment_lengths();
    puts("bitmap jpeg tests passed");
    return 0;
}
