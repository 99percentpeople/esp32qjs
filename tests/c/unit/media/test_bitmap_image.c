#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp32_mquickjs_bitmap_image.h"

static esp32_mquickjs_bitmap_transform_options_t default_options(
    uint32_t source_width,
    uint32_t source_height,
    uint32_t destination_width,
    uint32_t destination_height)
{
    esp32_mquickjs_bitmap_transform_options_t options = {
        .source_x = 0,
        .source_y = 0,
        .source_width = source_width,
        .source_height = source_height,
        .destination_x = 0,
        .destination_y = 0,
        .destination_width = destination_width,
        .destination_height = destination_height,
        .rotation = 0,
        .flip_x = false,
        .flip_y = false,
        .filter = ESP32_MQUICKJS_BITMAP_FILTER_NEAREST,
        .dither = ESP32_MQUICKJS_BITMAP_DITHER_NONE,
        .normalize = false,
        .threshold = 128,
    };

    return options;
}

static esp32_mquickjs_bitmap_view_t gray_source(const uint8_t *pixels,
                                                 size_t length,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint32_t stride)
{
    esp32_mquickjs_bitmap_view_t source = {
        .data = pixels,
        .length = length,
        .width = width,
        .height = height,
        .stride = stride,
        .format = ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
        .layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR,
        .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE,
        .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
    };

    return source;
}

static esp32_mquickjs_bitmap_target_t target_for(
    uint8_t *pixels,
    size_t length,
    uint32_t width,
    uint32_t height,
    uint32_t stride,
    esp32_mquickjs_bitmap_pixel_format_t format,
    esp32_mquickjs_bitmap_layout_t layout)
{
    esp32_mquickjs_bitmap_target_t target = {
        .data = pixels,
        .length = length,
        .width = width,
        .height = height,
        .stride = stride,
        .format = format,
        .layout = layout,
        .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE,
        .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
    };

    return target;
}

static void run_ok(const esp32_mquickjs_bitmap_view_t *source,
                   const esp32_mquickjs_bitmap_target_t *target,
                   const esp32_mquickjs_bitmap_transform_options_t *options)
{
    assert(esp32_mquickjs_bitmap_transform(
               source, target, options, NULL, NULL, NULL, NULL) ==
           ESP32_MQUICKJS_BITMAP_TRANSFORM_OK);
}

static void test_storage_and_input_encodings(void)
{
    uint32_t stride;
    size_t length;
    const uint8_t rgb565_le[] = {0x00, 0xf8};
    const uint8_t mono_msb[] = {0x80};
    const uint8_t mono_page_msb[] = {0x80};
    uint8_t output[3];
    esp32_mquickjs_bitmap_view_t source;
    esp32_mquickjs_bitmap_target_t target = target_for(
        output, sizeof(output), 1, 1, 3,
        ESP32_MQUICKJS_BITMAP_FORMAT_RGB888,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(1, 1, 1, 1);

    assert(esp32_mquickjs_bitmap_compute_storage(
        3, 2, ESP32_MQUICKJS_BITMAP_FORMAT_RGB888,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR, 12, &stride, &length));
    assert(stride == 12 && length == 21);
    assert(!esp32_mquickjs_bitmap_compute_storage(
        UINT32_MAX, 2, ESP32_MQUICKJS_BITMAP_FORMAT_RGB888,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR, 0, &stride, &length));
    assert(esp32_mquickjs_bitmap_compute_storage(
        3, 2, ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR, 0, &stride, &length));
    assert(stride == 2 && length == 4);
    assert(!esp32_mquickjs_bitmap_compute_storage(
        3, 2, ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
        ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8, 0, &stride, &length));
    assert(strcmp(esp32_mquickjs_bitmap_format_info(
                      ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4)->name,
                  "gray4") == 0);
    {
        esp32_mquickjs_bitmap_pixel_format_t parsed_format;

        assert(esp32_mquickjs_bitmap_parse_format("gray4", &parsed_format));
        assert(parsed_format == ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4);
        assert(!esp32_mquickjs_bitmap_parse_format("gray16", &parsed_format));
    }

    source = (esp32_mquickjs_bitmap_view_t){
        .data = rgb565_le,
        .length = sizeof(rgb565_le),
        .width = 1,
        .height = 1,
        .stride = 2,
        .format = ESP32_MQUICKJS_BITMAP_FORMAT_RGB565,
        .layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR,
        .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE,
        .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
    };
    run_ok(&source, &target, &options);
    assert(output[0] == 255 && output[1] == 0 && output[2] == 0);

    source.data = mono_msb;
    source.length = sizeof(mono_msb);
    source.stride = 1;
    source.format = ESP32_MQUICKJS_BITMAP_FORMAT_MONO1;
    source.layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR;
    source.bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB;
    run_ok(&source, &target, &options);
    assert(output[0] == 255 && output[1] == 255 && output[2] == 255);

    source.data = mono_page_msb;
    source.length = sizeof(mono_page_msb);
    source.layout = ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8;
    source.height = 8;
    options = default_options(1, 8, 1, 1);
    options.source_y = 0;
    options.source_height = 1;
    run_ok(&source, &target, &options);
    assert(output[0] == 255 && output[1] == 255 && output[2] == 255);
}

static void test_five_format_outputs(void)
{
    const uint8_t white[] = {255, 255, 255};
    esp32_mquickjs_bitmap_view_t source = {
        .data = white,
        .length = sizeof(white),
        .width = 1,
        .height = 1,
        .stride = 3,
        .format = ESP32_MQUICKJS_BITMAP_FORMAT_RGB888,
        .layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR,
        .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE,
        .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
    };
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(1, 1, 1, 1);
    uint8_t mono = 0;
    uint8_t gray4 = 0;
    uint8_t gray = 0;
    uint8_t rgb565[2] = {0};
    uint8_t rgb888[3] = {0};
    esp32_mquickjs_bitmap_target_t targets[] = {
        target_for(&mono, 1, 1, 1, 1,
                   ESP32_MQUICKJS_BITMAP_FORMAT_MONO1,
                   ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR),
        target_for(&gray4, 1, 1, 1, 1,
                   ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
                   ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR),
        target_for(&gray, 1, 1, 1, 1,
                   ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
                   ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR),
        target_for(rgb565, 2, 1, 1, 2,
                   ESP32_MQUICKJS_BITMAP_FORMAT_RGB565,
                   ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR),
        target_for(rgb888, 3, 1, 1, 3,
                   ESP32_MQUICKJS_BITMAP_FORMAT_RGB888,
                   ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR),
    };
    size_t i;

    for (i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        run_ok(&source, &targets[i], &options);
    }
    assert(mono == 0x01);
    assert(gray4 == 0xf0);
    assert(gray == 255);
    assert(rgb565[0] == 0xff && rgb565[1] == 0xff);
    assert(memcmp(rgb888, white, sizeof(white)) == 0);
}

static void test_every_format_pair(void)
{
    static const uint8_t mono_pixels[] = {0x01};
    static const uint8_t gray4_pixels[] = {0x80};
    static const uint8_t gray_pixels[] = {0x80};
    static const uint8_t red565_pixels[] = {0xf8, 0x00};
    static const uint8_t blue888_pixels[] = {0x00, 0x00, 0xff};
    static const uint8_t expected[5][5][3] = {
        {{0x01}, {0xf0}, {0xff}, {0xff, 0xff}, {0xff, 0xff, 0xff}},
        {{0x01}, {0x80}, {0x88}, {0x8c, 0x51}, {0x88, 0x88, 0x88}},
        {{0x01}, {0x80}, {0x80}, {0x84, 0x10}, {0x80, 0x80, 0x80}},
        {{0x00}, {0x40}, {0x4d}, {0xf8, 0x00}, {0xff, 0x00, 0x00}},
        {{0x00}, {0x10}, {0x1d}, {0x00, 0x1f}, {0x00, 0x00, 0xff}},
    };
    static const size_t source_lengths[] = {1, 1, 1, 2, 3};
    static const size_t target_lengths[] = {1, 1, 1, 2, 3};
    static const uint32_t strides[] = {1, 1, 1, 2, 3};
    const uint8_t *source_pixels[] = {
        mono_pixels, gray4_pixels, gray_pixels, red565_pixels, blue888_pixels,
    };
    esp32_mquickjs_bitmap_pixel_format_t formats[] = {
        ESP32_MQUICKJS_BITMAP_FORMAT_MONO1,
        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
        ESP32_MQUICKJS_BITMAP_FORMAT_RGB565,
        ESP32_MQUICKJS_BITMAP_FORMAT_RGB888,
    };
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(1, 1, 1, 1);
    size_t source_index;

    for (source_index = 0; source_index < 5; ++source_index) {
        esp32_mquickjs_bitmap_view_t source = {
            .data = source_pixels[source_index],
            .length = source_lengths[source_index],
            .width = 1,
            .height = 1,
            .stride = strides[source_index],
            .format = formats[source_index],
            .layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR,
            .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE,
            .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
        };
        size_t target_index;

        for (target_index = 0; target_index < 5; ++target_index) {
            uint8_t output[3] = {0};
            esp32_mquickjs_bitmap_target_t target = target_for(
                output, target_lengths[target_index], 1, 1,
                strides[target_index], formats[target_index],
                ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);

            run_ok(&source, &target, &options);
            assert(memcmp(output, expected[source_index][target_index],
                          target_lengths[target_index]) == 0);
        }
    }
}

static void test_gray4_packing_and_partial_write(void)
{
    const uint8_t gray_pixels[] = {0x00, 0x7f, 0xff};
    uint8_t packed[] = {0xcc, 0xca};
    uint8_t unpacked[3] = {0};
    esp32_mquickjs_bitmap_view_t gray =
        gray_source(gray_pixels, sizeof(gray_pixels), 3, 1, 3);
    esp32_mquickjs_bitmap_target_t gray4 = target_for(
        packed, sizeof(packed), 3, 1, 2,
        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(3, 1, 3, 1);
    esp32_mquickjs_bitmap_view_t packed_source;
    esp32_mquickjs_bitmap_target_t gray8;

    run_ok(&gray, &gray4, &options);
    assert(packed[0] == 0x07);
    assert(packed[1] == 0xfa);

    packed[1] = 0xf0;
    packed_source = (esp32_mquickjs_bitmap_view_t){
        .data = packed,
        .length = sizeof(packed),
        .width = 3,
        .height = 1,
        .stride = 2,
        .format = ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
        .layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR,
        .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE,
        .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
    };
    gray8 = target_for(unpacked, sizeof(unpacked), 3, 1, 3,
                       ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
                       ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    run_ok(&packed_source, &gray8, &options);
    assert(unpacked[0] == 0 && unpacked[1] == 119 && unpacked[2] == 255);
}

static void test_gray_rgb565_identity_fast_paths(void)
{
    const uint8_t gray4_pixels[] = {0x10, 0x7f, 0x20, 0xee};
    uint8_t rgb565_pixels[12];
    esp32_mquickjs_bitmap_view_t gray4 = {
        .data = gray4_pixels,
        .length = sizeof(gray4_pixels),
        .width = 5,
        .height = 1,
        .stride = 4,
        .format = ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
        .layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR,
        .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE,
        .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
    };
    esp32_mquickjs_bitmap_target_t rgb565 = target_for(
        rgb565_pixels, sizeof(rgb565_pixels), 5, 1, 12,
        ESP32_MQUICKJS_BITMAP_FORMAT_RGB565,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(3, 1, 3, 1);
    const uint8_t rgb565_be[] = {
        0xf8, 0x00, 0x07, 0xe0, 0x00, 0x1f, 0xcc, 0xcc,
    };
    const uint8_t rgb565_le[] = {
        0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00, 0xcc, 0xcc,
    };
    uint8_t gray8_pixels[] = {0xaa, 0xaa, 0xaa, 0xee};
    uint8_t gray4_output[] = {0xaa, 0xaa, 0xee};
    esp32_mquickjs_bitmap_view_t rgb565_source;
    esp32_mquickjs_bitmap_target_t gray8 = target_for(
        gray8_pixels, sizeof(gray8_pixels), 3, 1, 4,
        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    esp32_mquickjs_bitmap_target_t gray4_output_target = target_for(
        gray4_output, sizeof(gray4_output), 3, 1, 3,
        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);

    options.source_x = 1;
    options.destination_x = 1;
    memset(rgb565_pixels, 0xaa, sizeof(rgb565_pixels));
    run_ok(&gray4, &rgb565, &options);
    assert(rgb565_pixels[0] == 0xaa && rgb565_pixels[1] == 0xaa);
    assert(rgb565_pixels[2] == 0x00 && rgb565_pixels[3] == 0x00);
    assert(rgb565_pixels[4] == 0x73 && rgb565_pixels[5] == 0xae);
    assert(rgb565_pixels[6] == 0xff && rgb565_pixels[7] == 0xff);
    assert(rgb565_pixels[8] == 0xaa && rgb565_pixels[11] == 0xaa);

    rgb565.byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE;
    memset(rgb565_pixels, 0xaa, sizeof(rgb565_pixels));
    run_ok(&gray4, &rgb565, &options);
    assert(rgb565_pixels[2] == 0x00 && rgb565_pixels[3] == 0x00);
    assert(rgb565_pixels[4] == 0xae && rgb565_pixels[5] == 0x73);
    assert(rgb565_pixels[6] == 0xff && rgb565_pixels[7] == 0xff);

    options = default_options(3, 1, 3, 1);
    rgb565_source = (esp32_mquickjs_bitmap_view_t){
        .data = rgb565_be,
        .length = sizeof(rgb565_be),
        .width = 3,
        .height = 1,
        .stride = 8,
        .format = ESP32_MQUICKJS_BITMAP_FORMAT_RGB565,
        .layout = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR,
        .byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE,
        .bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB,
    };
    run_ok(&rgb565_source, &gray8, &options);
    assert(gray8_pixels[0] == 77 && gray8_pixels[1] == 149 &&
           gray8_pixels[2] == 29 && gray8_pixels[3] == 0xee);

    rgb565_source.data = rgb565_le;
    rgb565_source.byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE;
    run_ok(&rgb565_source, &gray4_output_target, &options);
    assert(gray4_output[0] == 0x49 && gray4_output[1] == 0x1a &&
           gray4_output[2] == 0xee);
}

static void forward_position(uint16_t rotation,
                             bool flip_x,
                             bool flip_y,
                             uint32_t width,
                             uint32_t height,
                             uint32_t x,
                             uint32_t y,
                             uint32_t *out_x,
                             uint32_t *out_y)
{
    uint32_t rotated_width = rotation == 90 || rotation == 270 ? height : width;
    uint32_t rotated_height = rotation == 90 || rotation == 270 ? width : height;
    uint32_t rx;
    uint32_t ry;

    if (rotation == 90) {
        rx = height - 1U - y;
        ry = x;
    } else if (rotation == 180) {
        rx = width - 1U - x;
        ry = height - 1U - y;
    } else if (rotation == 270) {
        rx = y;
        ry = width - 1U - x;
    } else {
        rx = x;
        ry = y;
    }
    *out_x = flip_x ? rotated_width - 1U - rx : rx;
    *out_y = flip_y ? rotated_height - 1U - ry : ry;
}

static void test_all_rotations_and_flips(void)
{
    const uint8_t pixels[] = {1, 2, 3, 4, 5, 6};
    const uint16_t rotations[] = {0, 90, 180, 270};
    esp32_mquickjs_bitmap_view_t source =
        gray_source(pixels, sizeof(pixels), 3, 2, 3);
    size_t rotation_index;
    unsigned flips;

    for (rotation_index = 0;
         rotation_index < sizeof(rotations) / sizeof(rotations[0]);
         ++rotation_index) {
        for (flips = 0; flips < 4; ++flips) {
            uint16_t rotation = rotations[rotation_index];
            uint32_t width = rotation == 90 || rotation == 270 ? 2 : 3;
            uint32_t height = rotation == 90 || rotation == 270 ? 3 : 2;
            uint8_t output[6] = {0};
            uint8_t expected[6] = {0};
            esp32_mquickjs_bitmap_target_t target = target_for(
                output, sizeof(output), width, height, width,
                ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
                ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
            esp32_mquickjs_bitmap_transform_options_t options =
                default_options(3, 2, width, height);
            uint32_t y;

            options.rotation = rotation;
            options.flip_x = (flips & 1U) != 0;
            options.flip_y = (flips & 2U) != 0;
            for (y = 0; y < 2; ++y) {
                uint32_t x;

                for (x = 0; x < 3; ++x) {
                    uint32_t dx;
                    uint32_t dy;

                    forward_position(rotation, options.flip_x, options.flip_y,
                                     3, 2, x, y, &dx, &dy);
                    expected[dy * width + dx] = pixels[y * 3U + x];
                }
            }
            run_ok(&source, &target, &options);
            assert(memcmp(output, expected, sizeof(output)) == 0);
        }
    }
}

static void test_crop_resize_bilinear_and_normalize(void)
{
    const uint8_t crop_pixels[] = {
        1, 10, 20, 2,
        3, 30, 40, 4,
    };
    const uint8_t bilinear_pixels[] = {0, 0, 0, 255};
    const uint8_t normalized_pixels[] = {100, 200};
    uint8_t output[8] = {0};
    esp32_mquickjs_bitmap_view_t source =
        gray_source(crop_pixels, sizeof(crop_pixels), 4, 2, 4);
    esp32_mquickjs_bitmap_target_t target = target_for(
        output, sizeof(output), 4, 2, 4,
        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(2, 2, 4, 2);

    options.source_x = 1;
    run_ok(&source, &target, &options);
    assert(output[0] == 10 && output[1] == 10 && output[2] == 20 &&
           output[3] == 20 && output[4] == 30 && output[7] == 40);

    memset(output, 0, sizeof(output));
    source = gray_source(bilinear_pixels, sizeof(bilinear_pixels), 2, 2, 2);
    target = target_for(output, sizeof(output), 1, 1, 1,
                        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
                        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    options = default_options(2, 2, 1, 1);
    options.filter = ESP32_MQUICKJS_BITMAP_FILTER_BILINEAR;
    run_ok(&source, &target, &options);
    assert(output[0] == 64);

    source = gray_source(normalized_pixels, sizeof(normalized_pixels), 2, 1, 2);
    target = target_for(output, sizeof(output), 2, 1, 2,
                        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
                        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    options = default_options(2, 1, 2, 1);
    options.normalize = true;
    run_ok(&source, &target, &options);
    assert(output[0] == 0 && output[1] == 255);

    output[0] = 0;
    target = target_for(output, 1, 2, 1, 1,
                        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY4,
                        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    run_ok(&source, &target, &options);
    assert(output[0] == 0x0f);
}

static void test_bayer_anchor_and_clipping(void)
{
    const uint8_t pixels[16] = {
        128, 128, 128, 128,
        128, 128, 128, 128,
        128, 128, 128, 128,
        128, 128, 128, 128,
    };
    uint8_t complete[4] = {0};
    uint8_t tiled[4] = {0};
    esp32_mquickjs_bitmap_view_t source =
        gray_source(pixels, sizeof(pixels), 4, 4, 4);
    esp32_mquickjs_bitmap_target_t target = target_for(
        complete, sizeof(complete), 4, 4, 4,
        ESP32_MQUICKJS_BITMAP_FORMAT_MONO1,
        ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8);
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(4, 4, 4, 4);
    esp32_mquickjs_bitmap_dirty_rect_t dirty;

    options.dither = ESP32_MQUICKJS_BITMAP_DITHER_BAYER_4X4;
    assert(esp32_mquickjs_bitmap_transform(
               &source, &target, &options, NULL, NULL, NULL, &dirty) ==
           ESP32_MQUICKJS_BITMAP_TRANSFORM_OK);
    assert(complete[0] == 0x05 && complete[1] == 0x0a &&
           complete[2] == 0x05 && complete[3] == 0x0a);

    target.data = tiled;
    options.source_width = 2;
    options.destination_width = 2;
    run_ok(&source, &target, &options);
    options.source_x = 2;
    options.destination_x = 2;
    run_ok(&source, &target, &options);
    assert(memcmp(complete, tiled, sizeof(complete)) == 0);

    memset(tiled, 0, sizeof(tiled));
    options = default_options(4, 4, 4, 4);
    options.destination_x = -2;
    assert(esp32_mquickjs_bitmap_transform(
               &source, &target, &options, NULL, NULL, NULL, &dirty) ==
           ESP32_MQUICKJS_BITMAP_TRANSFORM_OK);
    assert(dirty.x == 0 && dirty.y == 0 && dirty.width == 2 &&
           dirty.height == 4);
}

static bool mono_pixel(const uint8_t *pixels,
                       uint32_t stride,
                       esp32_mquickjs_bitmap_layout_t layout,
                       esp32_mquickjs_bitmap_bit_order_t bit_order,
                       uint32_t x,
                       uint32_t y)
{
    size_t offset;
    uint8_t bit;

    if (layout == ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8) {
        offset = ((size_t)y >> 3U) * stride + x;
        bit = (uint8_t)(y & 7U);
    } else {
        offset = (size_t)y * stride + (x >> 3U);
        bit = (uint8_t)(x & 7U);
    }
    if (bit_order == ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB) {
        bit = (uint8_t)(7U - bit);
    }
    return ((pixels[offset] >> bit) & 1U) != 0;
}

static void test_gray8_to_page_mono1_fast_path(void)
{
    static const uint8_t pixels[] = {
        0, 31, 63, 95, 127, 159, 191,
        223, 255, 17, 49, 81, 113, 145,
        177, 209, 241, 7, 39, 71, 103,
        135, 167, 199, 231, 23, 55, 87,
        119, 151, 183, 215, 247, 15, 47,
    };
    static const uint16_t rotations[] = {0, 90, 180, 270};
    esp32_mquickjs_bitmap_view_t source =
        gray_source(pixels, sizeof(pixels), 7, 5, 7);
    size_t rotation_index;
    unsigned variant;

    for (rotation_index = 0;
         rotation_index < sizeof(rotations) / sizeof(rotations[0]);
         ++rotation_index) {
        for (variant = 0; variant < 8; ++variant) {
            uint8_t linear[27];
            uint8_t page[27];
            esp32_mquickjs_bitmap_target_t linear_target;
            esp32_mquickjs_bitmap_target_t page_target;
            esp32_mquickjs_bitmap_transform_options_t options =
                default_options(7, 5, 9, 9);
            uint32_t y;

            memset(linear, 0, sizeof(linear));
            memset(page, 0, sizeof(page));
            linear_target = target_for(
                linear, sizeof(linear), 9, 9, 3,
                ESP32_MQUICKJS_BITMAP_FORMAT_MONO1,
                ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
            page_target = target_for(
                page, sizeof(page), 9, 9, 13,
                ESP32_MQUICKJS_BITMAP_FORMAT_MONO1,
                ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8);
            options.rotation = rotations[rotation_index];
            options.flip_x = (variant & 1U) != 0;
            options.flip_y = (variant & 2U) != 0;
            options.normalize = (variant & 4U) != 0;
            options.dither = (variant & 1U) != 0
                                 ? ESP32_MQUICKJS_BITMAP_DITHER_BAYER_4X4
                                 : ESP32_MQUICKJS_BITMAP_DITHER_NONE;
            options.destination_x = -1;
            options.destination_y = 1;
            linear_target.bit_order = (variant & 2U) != 0
                                          ? ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB
                                          : ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB;
            page_target.bit_order = linear_target.bit_order;

            run_ok(&source, &linear_target, &options);
            run_ok(&source, &page_target, &options);
            for (y = 0; y < 9; ++y) {
                uint32_t x;

                for (x = 0; x < 9; ++x) {
                    assert(mono_pixel(linear, linear_target.stride,
                                      linear_target.layout,
                                      linear_target.bit_order, x, y) ==
                           mono_pixel(page, page_target.stride,
                                      page_target.layout,
                                      page_target.bit_order, x, y));
                }
            }
        }
    }
}

typedef struct {
    unsigned checks;
} cancel_state_t;

static bool cancel_after_first_row(void *opaque)
{
    cancel_state_t *state = opaque;

    return state->checks++ != 0;
}

static void test_cancellation_and_invalid_memory_bounds(void)
{
    const uint8_t pixels[] = {1, 2, 3, 4};
    uint8_t output[4] = {0};
    esp32_mquickjs_bitmap_view_t source =
        gray_source(pixels, sizeof(pixels), 2, 2, 2);
    esp32_mquickjs_bitmap_target_t target = target_for(
        output, sizeof(output), 2, 2, 2,
        ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8,
        ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR);
    esp32_mquickjs_bitmap_transform_options_t options =
        default_options(2, 2, 2, 2);
    cancel_state_t cancel = {0};
    uint32_t rows = 0;

    options.normalize = true;
    assert(esp32_mquickjs_bitmap_transform(
               &source, &target, &options, cancel_after_first_row, &cancel,
               &rows, NULL) == ESP32_MQUICKJS_BITMAP_TRANSFORM_CANCELLED);
    assert(rows == 0 && output[0] == 0 && output[1] == 0 &&
           output[2] == 0 && output[3] == 0);

    options.normalize = false;
    cancel.checks = 0;
    assert(esp32_mquickjs_bitmap_transform(
               &source, &target, &options, cancel_after_first_row, &cancel,
               &rows, NULL) == ESP32_MQUICKJS_BITMAP_TRANSFORM_CANCELLED);
    assert(rows == 1 && output[0] == 1 && output[1] == 2 &&
           output[2] == 0 && output[3] == 0);

    target.length = 3;
    assert(esp32_mquickjs_bitmap_transform(
               &source, &target, &options, NULL, NULL, NULL, NULL) ==
           ESP32_MQUICKJS_BITMAP_TRANSFORM_INVALID);
}

int main(void)
{
    test_storage_and_input_encodings();
    test_five_format_outputs();
    test_every_format_pair();
    test_gray4_packing_and_partial_write();
    test_gray_rgb565_identity_fast_paths();
    test_all_rotations_and_flips();
    test_crop_resize_bilinear_and_normalize();
    test_bayer_anchor_and_clipping();
    test_gray8_to_page_mono1_fast_path();
    test_cancellation_and_invalid_memory_bounds();
    puts("bitmap image tests passed");
    return 0;
}
