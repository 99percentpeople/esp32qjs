#include "esp32_mquickjs_bitmap_image.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} bitmap_rgb_t;

static const uint8_t s_bayer_4x4[] = {
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5,
};

bool esp32_mquickjs_bitmap_compute_storage(
    uint32_t width,
    uint32_t height,
    esp32_mquickjs_bitmap_pixel_format_t format,
    esp32_mquickjs_bitmap_layout_t layout,
    uint32_t requested_stride,
    uint32_t *out_stride,
    size_t *out_length)
{
    uint32_t rows;
    uint32_t row_bytes;
    uint32_t stride;

    if (width == 0 || height == 0 || out_stride == NULL || out_length == NULL) {
        return false;
    }
    switch (format) {
        case ESP32_MQUICKJS_BITMAP_FORMAT_MONO1:
            if (layout == ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8) {
                rows = (height + 7U) / 8U;
                row_bytes = width;
            } else if (layout == ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR) {
                rows = height;
                row_bytes = (width + 7U) / 8U;
            } else {
                return false;
            }
            break;
        case ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8:
            if (layout != ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR) {
                return false;
            }
            rows = height;
            row_bytes = width;
            break;
        case ESP32_MQUICKJS_BITMAP_FORMAT_RGB565:
            if (layout != ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR ||
                width > UINT32_MAX / 2U) {
                return false;
            }
            rows = height;
            row_bytes = width * 2U;
            break;
        case ESP32_MQUICKJS_BITMAP_FORMAT_RGB888:
            if (layout != ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR ||
                width > UINT32_MAX / 3U) {
                return false;
            }
            rows = height;
            row_bytes = width * 3U;
            break;
        default:
            return false;
    }
    stride = requested_stride == 0 ? row_bytes : requested_stride;
    if (stride < row_bytes || (size_t)(rows - 1U) >
                                  (SIZE_MAX - row_bytes) / stride) {
        return false;
    }
    *out_stride = stride;
    *out_length = (size_t)(rows - 1U) * stride + row_bytes;
    return true;
}

static bool view_is_valid(const esp32_mquickjs_bitmap_view_t *view)
{
    uint32_t stride;
    size_t length;

    return view != NULL && view->data != NULL &&
           esp32_mquickjs_bitmap_compute_storage(
               view->width, view->height, view->format, view->layout,
               view->stride, &stride, &length) &&
           stride == view->stride && length <= view->length &&
           (view->format != ESP32_MQUICKJS_BITMAP_FORMAT_RGB565 ||
            view->byte_order == ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE ||
            view->byte_order == ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE) &&
           (view->format != ESP32_MQUICKJS_BITMAP_FORMAT_MONO1 ||
            view->bit_order == ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB ||
            view->bit_order == ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB);
}

static bool target_is_valid(const esp32_mquickjs_bitmap_target_t *target)
{
    esp32_mquickjs_bitmap_view_t view;

    if (target == NULL) {
        return false;
    }
    view.data = target->data;
    view.length = target->length;
    view.width = target->width;
    view.height = target->height;
    view.stride = target->stride;
    view.format = target->format;
    view.layout = target->layout;
    view.byte_order = target->byte_order;
    view.bit_order = target->bit_order;
    return view_is_valid(&view);
}

static uint8_t expand5(uint16_t value)
{
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t expand6(uint16_t value)
{
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static bitmap_rgb_t read_pixel(const esp32_mquickjs_bitmap_view_t *source,
                               uint32_t x,
                               uint32_t y)
{
    bitmap_rgb_t result = {0, 0, 0};
    size_t offset;

    switch (source->format) {
        case ESP32_MQUICKJS_BITMAP_FORMAT_MONO1: {
            uint8_t byte;
            uint8_t bit;

            if (source->layout == ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8) {
                offset = ((size_t)y >> 3U) * source->stride + x;
                bit = (uint8_t)(y & 7U);
            } else {
                offset = (size_t)y * source->stride + (x >> 3U);
                bit = (uint8_t)(x & 7U);
            }
            if (source->bit_order == ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB) {
                bit = (uint8_t)(7U - bit);
            }
            byte = (source->data[offset] >> bit) & 1U ? 255U : 0U;
            result.r = byte;
            result.g = byte;
            result.b = byte;
            break;
        }
        case ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8:
            result.r = source->data[(size_t)y * source->stride + x];
            result.g = result.r;
            result.b = result.r;
            break;
        case ESP32_MQUICKJS_BITMAP_FORMAT_RGB565: {
            uint32_t color;

            offset = (size_t)y * source->stride + (size_t)x * 2U;
            if (source->byte_order == ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE) {
                color = (uint16_t)source->data[offset] |
                        (uint16_t)((uint16_t)source->data[offset + 1U] << 8U);
            } else {
                color = (uint16_t)((uint16_t)source->data[offset] << 8U) |
                        source->data[offset + 1U];
            }
            result.r = expand5((uint16_t)((color >> 11U) & 0x1fU));
            result.g = expand6((uint16_t)((color >> 5U) & 0x3fU));
            result.b = expand5((uint16_t)(color & 0x1fU));
            break;
        }
        case ESP32_MQUICKJS_BITMAP_FORMAT_RGB888:
            offset = (size_t)y * source->stride + (size_t)x * 3U;
            result.r = source->data[offset];
            result.g = source->data[offset + 1U];
            result.b = source->data[offset + 2U];
            break;
        default:
            break;
    }
    return result;
}

static uint8_t luminance(bitmap_rgb_t color)
{
    return (uint8_t)(((uint32_t)77U * color.r +
                      (uint32_t)150U * color.g +
                      (uint32_t)29U * color.b + 128U) >> 8U);
}

static uint32_t bilinear_coordinate(uint32_t position,
                                    uint32_t destination_size,
                                    uint32_t source_size)
{
    int64_t coordinate;
    int64_t maximum = (int64_t)(source_size - 1U) << 16U;

    coordinate = (int64_t)((((uint64_t)position * 2ULL + 1ULL) *
                            source_size * 32768ULL) /
                           destination_size) -
                 32768LL;
    if (coordinate < 0) {
        return 0;
    }
    if (coordinate > maximum) {
        return (uint32_t)maximum;
    }
    return (uint32_t)coordinate;
}

static void inverse_rotate(uint16_t rotation,
                           uint32_t source_width,
                           uint32_t source_height,
                           uint32_t rotated_x,
                           uint32_t rotated_y,
                           uint32_t *out_x,
                           uint32_t *out_y)
{
    switch (rotation) {
        case 90:
            *out_x = rotated_y;
            *out_y = source_height - 1U - rotated_x;
            break;
        case 180:
            *out_x = source_width - 1U - rotated_x;
            *out_y = source_height - 1U - rotated_y;
            break;
        case 270:
            *out_x = source_width - 1U - rotated_y;
            *out_y = rotated_x;
            break;
        default:
            *out_x = rotated_x;
            *out_y = rotated_y;
            break;
    }
}

static bitmap_rgb_t sample_nearest(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_transform_options_t *options,
    uint32_t rotated_width,
    uint32_t rotated_height,
    uint32_t x,
    uint32_t y)
{
    uint32_t rx = (uint32_t)(((uint64_t)x * rotated_width) /
                             options->destination_width);
    uint32_t ry = (uint32_t)(((uint64_t)y * rotated_height) /
                             options->destination_height);
    uint32_t sx;
    uint32_t sy;

    if (options->flip_x) {
        rx = rotated_width - 1U - rx;
    }
    if (options->flip_y) {
        ry = rotated_height - 1U - ry;
    }
    inverse_rotate(options->rotation, options->source_width,
                   options->source_height, rx, ry, &sx, &sy);
    return read_pixel(source, options->source_x + sx,
                      options->source_y + sy);
}

static bitmap_rgb_t interpolate(bitmap_rgb_t c00,
                                bitmap_rgb_t c10,
                                bitmap_rgb_t c01,
                                bitmap_rgb_t c11,
                                uint32_t fx,
                                uint32_t fy)
{
    uint32_t inverse_x = 65536U - fx;
    uint32_t inverse_y = 65536U - fy;
    uint32_t top;
    uint32_t bottom;
    bitmap_rgb_t result;

#define INTERPOLATE_CHANNEL(channel)                                            \
    top = ((uint32_t)c00.channel * inverse_x +                           \
           (uint32_t)c10.channel * fx + 32768U) >> 16U;                  \
    bottom = ((uint32_t)c01.channel * inverse_x +                        \
              (uint32_t)c11.channel * fx + 32768U) >> 16U;               \
    result.channel = (uint8_t)((top * inverse_y + bottom * fy + 32768U) >> 16U)

    INTERPOLATE_CHANNEL(r);
    INTERPOLATE_CHANNEL(g);
    INTERPOLATE_CHANNEL(b);
#undef INTERPOLATE_CHANNEL
    return result;
}

static bitmap_rgb_t sample_bilinear(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_transform_options_t *options,
    uint32_t rotated_width,
    uint32_t rotated_height,
    uint32_t x,
    uint32_t y)
{
    uint32_t rx_fp = bilinear_coordinate(
        x, options->destination_width, rotated_width);
    uint32_t ry_fp = bilinear_coordinate(
        y, options->destination_height, rotated_height);
    uint32_t rx0;
    uint32_t ry0;
    uint32_t rx1;
    uint32_t ry1;
    uint32_t sx00;
    uint32_t sy00;
    uint32_t sx10;
    uint32_t sy10;
    uint32_t sx01;
    uint32_t sy01;
    uint32_t sx11;
    uint32_t sy11;

    if (options->flip_x) {
        rx_fp = ((rotated_width - 1U) << 16U) - rx_fp;
    }
    if (options->flip_y) {
        ry_fp = ((rotated_height - 1U) << 16U) - ry_fp;
    }
    rx0 = rx_fp >> 16U;
    ry0 = ry_fp >> 16U;
    rx1 = rx0 + 1U < rotated_width ? rx0 + 1U : rx0;
    ry1 = ry0 + 1U < rotated_height ? ry0 + 1U : ry0;
    inverse_rotate(options->rotation, options->source_width,
                   options->source_height, rx0, ry0, &sx00, &sy00);
    inverse_rotate(options->rotation, options->source_width,
                   options->source_height, rx1, ry0, &sx10, &sy10);
    inverse_rotate(options->rotation, options->source_width,
                   options->source_height, rx0, ry1, &sx01, &sy01);
    inverse_rotate(options->rotation, options->source_width,
                   options->source_height, rx1, ry1, &sx11, &sy11);
    return interpolate(
        read_pixel(source, options->source_x + sx00,
                   options->source_y + sy00),
        read_pixel(source, options->source_x + sx10,
                   options->source_y + sy10),
        read_pixel(source, options->source_x + sx01,
                   options->source_y + sy01),
        read_pixel(source, options->source_x + sx11,
                   options->source_y + sy11),
        rx_fp & 0xffffU, ry_fp & 0xffffU);
}

static bool source_luminance_range(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_transform_options_t *options,
    esp32_mquickjs_bitmap_cancel_fn_t cancel,
    void *cancel_opaque,
    uint8_t *out_minimum,
    uint8_t *out_maximum)
{
    uint8_t minimum = 255U;
    uint8_t maximum = 0U;
    uint32_t y;

    if (source->format == ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8) {
        for (y = 0; y < options->source_height; ++y) {
            const uint8_t *row = source->data +
                (size_t)(options->source_y + y) * source->stride +
                options->source_x;
            uint32_t x;

            if (cancel != NULL && cancel(cancel_opaque)) {
                return false;
            }
            for (x = 0; x < options->source_width; ++x) {
                uint8_t gray = row[x];

                if (gray < minimum) {
                    minimum = gray;
                }
                if (gray > maximum) {
                    maximum = gray;
                }
            }
        }
        *out_minimum = minimum;
        *out_maximum = maximum;
        return true;
    }

    for (y = 0; y < options->source_height; ++y) {
        uint32_t x;

        if (cancel != NULL && cancel(cancel_opaque)) {
            return false;
        }

        for (x = 0; x < options->source_width; ++x) {
            uint8_t gray = luminance(read_pixel(
                source, options->source_x + x, options->source_y + y));

            if (gray < minimum) {
                minimum = gray;
            }
            if (gray > maximum) {
                maximum = gray;
            }
        }
    }
    *out_minimum = minimum;
    *out_maximum = maximum;
    return true;
}

typedef struct {
    uint32_t coordinate;
    uint32_t quotient;
    uint32_t remainder;
    uint32_t error;
    uint32_t divisor;
} nearest_axis_t;

static uint8_t normalize_gray(uint8_t gray,
                              uint8_t minimum,
                              uint8_t maximum);
static bool dither_enabled(uint8_t gray,
                           esp32_mquickjs_bitmap_dither_t dither,
                           uint8_t threshold,
                           uint32_t absolute_x,
                           uint32_t absolute_y);

static nearest_axis_t nearest_axis_start(uint32_t position,
                                         uint32_t source_size,
                                         uint32_t destination_size)
{
    uint64_t scaled = (uint64_t)position * source_size;
    nearest_axis_t axis = {
        .coordinate = (uint32_t)(scaled / destination_size),
        .quotient = source_size / destination_size,
        .remainder = source_size % destination_size,
        .error = (uint32_t)(scaled % destination_size),
        .divisor = destination_size,
    };

    return axis;
}

static void nearest_axis_advance(nearest_axis_t *axis)
{
    axis->coordinate += axis->quotient;
    axis->error += axis->remainder;
    if (axis->error >= axis->divisor) {
        axis->error -= axis->divisor;
        ++axis->coordinate;
    }
}

static uint8_t sample_gray8_nearest(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_transform_options_t *options,
    uint32_t rotated_width,
    uint32_t rotated_height,
    uint32_t rotated_x,
    uint32_t rotated_y)
{
    uint32_t source_x;
    uint32_t source_y;

    if (options->flip_x) {
        rotated_x = rotated_width - 1U - rotated_x;
    }
    if (options->flip_y) {
        rotated_y = rotated_height - 1U - rotated_y;
    }
    inverse_rotate(options->rotation, options->source_width,
                   options->source_height, rotated_x, rotated_y,
                   &source_x, &source_y);
    return source->data[
        (size_t)(options->source_y + source_y) * source->stride +
        options->source_x + source_x];
}

static esp32_mquickjs_bitmap_transform_result_t
transform_gray8_to_page_mono1_nearest(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_target_t *target,
    const esp32_mquickjs_bitmap_transform_options_t *options,
    int32_t clipped_x0,
    int32_t clipped_y0,
    int32_t clipped_x1,
    int32_t clipped_y1,
    uint32_t rotated_width,
    uint32_t rotated_height,
    uint8_t minimum,
    uint8_t maximum,
    esp32_mquickjs_bitmap_cancel_fn_t cancel,
    void *cancel_opaque,
    uint32_t *out_rows_completed,
    esp32_mquickjs_bitmap_dirty_rect_t *out_dirty)
{
    int32_t y;

    for (y = clipped_y0; y < clipped_y1; ++y) {
        uint32_t relative_y = (uint32_t)(
            (int64_t)y - options->destination_y);
        nearest_axis_t y_axis = nearest_axis_start(
            relative_y, rotated_height, options->destination_height);
        uint32_t relative_x = (uint32_t)(
            (int64_t)clipped_x0 - options->destination_x);
        nearest_axis_t x_axis = nearest_axis_start(
            relative_x, rotated_width, options->destination_width);
        int32_t x;

        if (cancel != NULL && cancel(cancel_opaque)) {
            return ESP32_MQUICKJS_BITMAP_TRANSFORM_CANCELLED;
        }
        for (x = clipped_x0; x < clipped_x1; ++x) {
            uint8_t gray = sample_gray8_nearest(
                source, options, rotated_width, rotated_height,
                x_axis.coordinate, y_axis.coordinate);
            size_t offset = ((size_t)(uint32_t)y >> 3U) * target->stride +
                            (uint32_t)x;
            uint8_t bit = (uint8_t)((uint32_t)y & 7U);
            uint8_t mask;

            if (options->normalize) {
                gray = normalize_gray(gray, minimum, maximum);
            }
            if (target->bit_order == ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB) {
                bit = (uint8_t)(7U - bit);
            }
            mask = (uint8_t)(1U << bit);
            if (dither_enabled(gray, options->dither, options->threshold,
                               (uint32_t)x, (uint32_t)y)) {
                target->data[offset] |= mask;
            } else {
                target->data[offset] &= (uint8_t)~mask;
            }
            nearest_axis_advance(&x_axis);
        }
        if (out_rows_completed != NULL) {
            ++*out_rows_completed;
        }
    }
    if (out_dirty != NULL) {
        out_dirty->x = clipped_x0;
        out_dirty->y = clipped_y0;
        out_dirty->width = (uint32_t)(clipped_x1 - clipped_x0);
        out_dirty->height = (uint32_t)(clipped_y1 - clipped_y0);
    }
    return ESP32_MQUICKJS_BITMAP_TRANSFORM_OK;
}

static uint8_t normalize_gray(uint8_t gray,
                              uint8_t minimum,
                              uint8_t maximum)
{
    uint32_t range = (uint32_t)maximum - minimum;

    if (range == 0) {
        return gray;
    }
    return (uint8_t)(((uint32_t)(gray - minimum) * 255U + range / 2U) /
                     range);
}

static bool dither_enabled(uint8_t gray,
                           esp32_mquickjs_bitmap_dither_t dither,
                           uint8_t threshold,
                           uint32_t absolute_x,
                           uint32_t absolute_y)
{
    int adjusted_threshold;

    if (dither == ESP32_MQUICKJS_BITMAP_DITHER_NONE) {
        return gray >= threshold;
    }
    adjusted_threshold =
        ((int)s_bayer_4x4[((absolute_y & 3U) << 2U) |
                          (absolute_x & 3U)] << 4) +
        8 + (int)threshold - 128;
    if (adjusted_threshold < 0) {
        adjusted_threshold = 0;
    } else if (adjusted_threshold > 255) {
        adjusted_threshold = 255;
    }
    return gray >= (uint8_t)adjusted_threshold;
}

static void write_pixel(const esp32_mquickjs_bitmap_target_t *target,
                        uint32_t x,
                        uint32_t y,
                        bitmap_rgb_t color,
                        uint8_t gray,
                        const esp32_mquickjs_bitmap_transform_options_t *options)
{
    size_t offset;

    switch (target->format) {
        case ESP32_MQUICKJS_BITMAP_FORMAT_MONO1: {
            uint8_t bit;
            uint8_t mask;

            if (target->layout == ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8) {
                offset = ((size_t)y >> 3U) * target->stride + x;
                bit = (uint8_t)(y & 7U);
            } else {
                offset = (size_t)y * target->stride + (x >> 3U);
                bit = (uint8_t)(x & 7U);
            }
            if (target->bit_order == ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB) {
                bit = (uint8_t)(7U - bit);
            }
            mask = (uint8_t)(1U << bit);
            if (dither_enabled(gray, options->dither, options->threshold,
                               x, y)) {
                target->data[offset] |= mask;
            } else {
                target->data[offset] &= (uint8_t)~mask;
            }
            break;
        }
        case ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8:
            target->data[(size_t)y * target->stride + x] = gray;
            break;
        case ESP32_MQUICKJS_BITMAP_FORMAT_RGB565: {
            uint16_t packed = (uint16_t)(((uint16_t)(color.r >> 3U) << 11U) |
                                         ((uint16_t)(color.g >> 2U) << 5U) |
                                         (uint16_t)(color.b >> 3U));

            offset = (size_t)y * target->stride + (size_t)x * 2U;
            if (target->byte_order == ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE) {
                target->data[offset] = (uint8_t)packed;
                target->data[offset + 1U] = (uint8_t)(packed >> 8U);
            } else {
                target->data[offset] = (uint8_t)(packed >> 8U);
                target->data[offset + 1U] = (uint8_t)packed;
            }
            break;
        }
        case ESP32_MQUICKJS_BITMAP_FORMAT_RGB888:
            offset = (size_t)y * target->stride + (size_t)x * 3U;
            target->data[offset] = color.r;
            target->data[offset + 1U] = color.g;
            target->data[offset + 2U] = color.b;
            break;
        default:
            break;
    }
}

static bool options_are_valid(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_target_t *target,
    const esp32_mquickjs_bitmap_transform_options_t *options)
{
    if (options == NULL || options->source_width == 0 ||
        options->source_height == 0 || options->destination_width == 0 ||
        options->destination_height == 0 ||
        options->source_x >= source->width ||
        options->source_y >= source->height ||
        options->source_width > source->width - options->source_x ||
        options->source_height > source->height - options->source_y ||
        (options->rotation != 0 && options->rotation != 90 &&
         options->rotation != 180 && options->rotation != 270) ||
        (options->filter != ESP32_MQUICKJS_BITMAP_FILTER_NEAREST &&
         options->filter != ESP32_MQUICKJS_BITMAP_FILTER_BILINEAR) ||
        (options->dither != ESP32_MQUICKJS_BITMAP_DITHER_NONE &&
         options->dither != ESP32_MQUICKJS_BITMAP_DITHER_BAYER_4X4)) {
        return false;
    }
    if (options->normalize &&
        target->format != ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8 &&
        target->format != ESP32_MQUICKJS_BITMAP_FORMAT_MONO1) {
        return false;
    }
    if ((options->dither != ESP32_MQUICKJS_BITMAP_DITHER_NONE ||
         options->threshold != 128U) &&
        target->format != ESP32_MQUICKJS_BITMAP_FORMAT_MONO1) {
        return false;
    }
    return true;
}

esp32_mquickjs_bitmap_transform_result_t esp32_mquickjs_bitmap_transform(
    const esp32_mquickjs_bitmap_view_t *source,
    const esp32_mquickjs_bitmap_target_t *target,
    const esp32_mquickjs_bitmap_transform_options_t *options,
    esp32_mquickjs_bitmap_cancel_fn_t cancel,
    void *cancel_opaque,
    uint32_t *out_rows_completed,
    esp32_mquickjs_bitmap_dirty_rect_t *out_dirty)
{
    int64_t destination_x1;
    int64_t destination_y1;
    int32_t clipped_x0;
    int32_t clipped_y0;
    int32_t clipped_x1;
    int32_t clipped_y1;
    uint32_t rotated_width;
    uint32_t rotated_height;
    uint8_t minimum = 0;
    uint8_t maximum = 255;
    int32_t y;

    if (out_rows_completed != NULL) {
        *out_rows_completed = 0;
    }
    if (out_dirty != NULL) {
        out_dirty->x = 0;
        out_dirty->y = 0;
        out_dirty->width = 0;
        out_dirty->height = 0;
    }
    if (!view_is_valid(source) || !target_is_valid(target) ||
        !options_are_valid(source, target, options)) {
        return ESP32_MQUICKJS_BITMAP_TRANSFORM_INVALID;
    }
    destination_x1 = (int64_t)options->destination_x +
                     options->destination_width;
    destination_y1 = (int64_t)options->destination_y +
                     options->destination_height;
    if (destination_x1 > INT32_MAX || destination_y1 > INT32_MAX) {
        return ESP32_MQUICKJS_BITMAP_TRANSFORM_INVALID;
    }
    clipped_x0 = options->destination_x < 0 ? 0 : options->destination_x;
    clipped_y0 = options->destination_y < 0 ? 0 : options->destination_y;
    clipped_x1 = destination_x1 > target->width
                     ? (int32_t)target->width
                     : (int32_t)destination_x1;
    clipped_y1 = destination_y1 > target->height
                     ? (int32_t)target->height
                     : (int32_t)destination_y1;
    if (clipped_x1 <= clipped_x0 || clipped_y1 <= clipped_y0) {
        return ESP32_MQUICKJS_BITMAP_TRANSFORM_OK;
    }
    rotated_width = options->rotation == 90 || options->rotation == 270
                        ? options->source_height
                        : options->source_width;
    rotated_height = options->rotation == 90 || options->rotation == 270
                         ? options->source_width
                         : options->source_height;
    if (options->normalize) {
        if (!source_luminance_range(source, options, cancel, cancel_opaque,
                                    &minimum, &maximum)) {
            return ESP32_MQUICKJS_BITMAP_TRANSFORM_CANCELLED;
        }
    }
    if (source->format == ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8 &&
        source->layout == ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR &&
        target->format == ESP32_MQUICKJS_BITMAP_FORMAT_MONO1 &&
        target->layout == ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8 &&
        options->filter == ESP32_MQUICKJS_BITMAP_FILTER_NEAREST) {
        return transform_gray8_to_page_mono1_nearest(
            source, target, options, clipped_x0, clipped_y0,
            clipped_x1, clipped_y1, rotated_width, rotated_height,
            minimum, maximum, cancel, cancel_opaque, out_rows_completed,
            out_dirty);
    }
    for (y = clipped_y0; y < clipped_y1; ++y) {
        uint32_t relative_y;
        int32_t x;

        if (cancel != NULL && cancel(cancel_opaque)) {
            return ESP32_MQUICKJS_BITMAP_TRANSFORM_CANCELLED;
        }
        relative_y = (uint32_t)(y - options->destination_y);
        for (x = clipped_x0; x < clipped_x1; ++x) {
            uint32_t relative_x = (uint32_t)(x - options->destination_x);
            bitmap_rgb_t color =
                options->filter == ESP32_MQUICKJS_BITMAP_FILTER_BILINEAR
                    ? sample_bilinear(source, options, rotated_width,
                                      rotated_height, relative_x, relative_y)
                    : sample_nearest(source, options, rotated_width,
                                     rotated_height, relative_x, relative_y);
            uint8_t gray = luminance(color);

            if (options->normalize) {
                gray = normalize_gray(gray, minimum, maximum);
                color.r = gray;
                color.g = gray;
                color.b = gray;
            }
            write_pixel(target, (uint32_t)x, (uint32_t)y, color, gray,
                        options);
        }
        if (out_rows_completed != NULL) {
            ++*out_rows_completed;
        }
    }
    if (out_dirty != NULL) {
        out_dirty->x = clipped_x0;
        out_dirty->y = clipped_y0;
        out_dirty->width = (uint32_t)(clipped_x1 - clipped_x0);
        out_dirty->height = (uint32_t)(clipped_y1 - clipped_y0);
    }
    return ESP32_MQUICKJS_BITMAP_TRANSFORM_OK;
}
