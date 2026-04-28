#include "esp32_mquickjs_display_buffer_internal.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

#include "utils/esp32_mquickjs_byte_source.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_psram.h"

#define DISPLAY_BUFFER_SMALL_INTERNAL_LIMIT 8192U
#define DISPLAY_BUFFER_STAGED_VIEW_KEY "__esp32qjsDisplayBufferStagedView"
#define DISPLAY_BUFFER_STAGED_CHUNKS_KEY "__esp32qjsDisplayBufferStagedChunks"

typedef struct {
    esp32_mquickjs_display_buffer_t *buffer;
    esp32_mquickjs_display_buffer_rect_t rect;
    uint32_t chunk_bytes;
    uint8_t *scratch;
    size_t scratch_capacity;
    int iter_y;
    int iter_remaining;
    JSValue owner;
    bool iterating;
} display_buffer_span_source_t;

static const char *format_name(uint8_t format)
{
    return format == DISPLAY_BUFFER_FORMAT_RGB565 ? "rgb565" : "mono1";
}

static const char *layout_name(uint8_t layout)
{
    return layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8 ? "page-y8" : "linear";
}

static bool string_equals(const char *value, const char *expected)
{
    return value != NULL && strcmp(value, expected) == 0;
}

bool value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < 0) {
        return false;
    }
    *out_value = (uint32_t)raw_value;
    return true;
}

bool value_to_i32(JSContext *ctx, JSValue value, int32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return false;
    }
    *out_value = (int32_t)raw_value;
    return true;
}

uint16_t normalize_color(JSContext *ctx,
                         uint8_t format,
                         JSValue value,
                         uint16_t fallback,
                         bool *ok)
{
    uint32_t raw = fallback;

    if (ok != NULL) {
        *ok = true;
    }
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        return fallback;
    }
    if (format == DISPLAY_BUFFER_FORMAT_MONO1) {
        if (JS_IsBool(value) || !value_to_u32(ctx, value, &raw) || raw > 1U) {
            if (ok != NULL) {
                *ok = false;
            }
            return 0;
        }
        return (uint16_t)raw;
    }
    if (!value_to_u32(ctx, value, &raw) || raw > 0xffffU) {
        if (ok != NULL) {
            *ok = false;
        }
        return 0;
    }
    return (uint16_t)raw;
}

static JSValue get_option(JSContext *ctx, JSValue options, const char *name)
{
    return JS_GetPropertyStr(ctx, options, name);
}

static bool get_u32_option(JSContext *ctx,
                           JSValue options,
                           const char *name,
                           uint32_t *out_value,
                           bool required,
                           const char *api_name)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool ok = true;

    *property = get_option(ctx, options, name);
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (JS_IsUndefined(*property) || JS_IsNull(*property)) {
        if (required) {
            JS_ThrowTypeError(ctx, "%s requires option '%s'", api_name, name);
            ok = false;
        }
    } else if (!value_to_u32(ctx, *property, out_value)) {
        JS_ThrowTypeError(ctx, "%s option '%s' expects a non-negative integer", api_name, name);
        ok = false;
    }
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static bool get_string_option(JSContext *ctx,
                              JSValue options,
                              const char *name,
                              const char **out_value,
                              JSCStringBuf *out_buf,
                              bool required,
                              const char *api_name)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool ok = true;

    *property = get_option(ctx, options, name);
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (JS_IsUndefined(*property) || JS_IsNull(*property)) {
        if (required) {
            JS_ThrowTypeError(ctx, "%s requires option '%s'", api_name, name);
            ok = false;
        }
    } else {
        *out_value = JS_ToCString(ctx, *property, out_buf);
        if (*out_value == NULL) {
            ok = false;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static bool parse_format(const char *name, uint8_t *out_format)
{
    if (string_equals(name, "mono1")) {
        *out_format = DISPLAY_BUFFER_FORMAT_MONO1;
        return true;
    }
    if (string_equals(name, "rgb565") || string_equals(name, "rgb565be") || string_equals(name, "rgb565le")) {
        *out_format = DISPLAY_BUFFER_FORMAT_RGB565;
        return true;
    }
    return false;
}

static bool parse_layout(const char *name, uint8_t format, uint8_t *out_layout)
{
    if (name == NULL) {
        *out_layout = format == DISPLAY_BUFFER_FORMAT_MONO1 ? DISPLAY_BUFFER_LAYOUT_PAGE_Y8
                                                            : DISPLAY_BUFFER_LAYOUT_LINEAR;
        return true;
    }
    if (string_equals(name, "linear")) {
        *out_layout = DISPLAY_BUFFER_LAYOUT_LINEAR;
        return true;
    }
    if (string_equals(name, "page-y8")) {
        *out_layout = DISPLAY_BUFFER_LAYOUT_PAGE_Y8;
        return true;
    }
    return false;
}

static bool parse_storage(const char *name, uint8_t *out_storage)
{
    if (name == NULL || string_equals(name, "auto")) {
        *out_storage = DISPLAY_BUFFER_STORAGE_AUTO;
        return true;
    }
    if (string_equals(name, "internal")) {
        *out_storage = DISPLAY_BUFFER_STORAGE_INTERNAL;
        return true;
    }
    if (string_equals(name, "psram")) {
        *out_storage = DISPLAY_BUFFER_STORAGE_PSRAM;
        return true;
    }
    if (string_equals(name, "dma")) {
        *out_storage = DISPLAY_BUFFER_STORAGE_DMA;
        return true;
    }
    return false;
}

static bool compute_layout(uint32_t width,
                           uint32_t height,
                           uint8_t format,
                           uint8_t layout,
                           uint32_t requested_stride,
                           uint32_t requested_page_height,
                           uint16_t *out_stride,
                           uint16_t *out_page_height,
                           size_t *out_byte_length)
{
    uint32_t min_stride = 0;
    uint32_t page_height = 1;
    size_t rows = height;
    size_t byte_length;

    if (width == 0 || height == 0 ||
        width > DISPLAY_BUFFER_MAX_DIMENSION || height > DISPLAY_BUFFER_MAX_DIMENSION) {
        return false;
    }

    if (format == DISPLAY_BUFFER_FORMAT_RGB565) {
        if (layout != DISPLAY_BUFFER_LAYOUT_LINEAR) {
            return false;
        }
        min_stride = width * 2U;
        page_height = 1;
        rows = height;
    } else if (layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8) {
        min_stride = width;
        page_height = requested_page_height == 0 ? 8U : requested_page_height;
        if (page_height != 8U) {
            return false;
        }
        rows = (height + 7U) / 8U;
    } else {
        min_stride = (width + 7U) / 8U;
        page_height = 1;
        rows = height;
    }

    if (requested_stride != 0) {
        if (requested_stride < min_stride || requested_stride > UINT16_MAX) {
            return false;
        }
        min_stride = requested_stride;
    }
    if (min_stride > UINT16_MAX || page_height > UINT16_MAX) {
        return false;
    }
    byte_length = rows * (size_t)min_stride;
    if (byte_length == 0 || byte_length > SIZE_MAX / 2U) {
        return false;
    }

    *out_stride = (uint16_t)min_stride;
    *out_page_height = (uint16_t)page_height;
    *out_byte_length = byte_length;
    return true;
}

static uint32_t allocation_caps(uint8_t storage, size_t byte_length)
{
    if (storage == DISPLAY_BUFFER_STORAGE_INTERNAL) {
        return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    }
    if (storage == DISPLAY_BUFFER_STORAGE_PSRAM) {
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    }
    if (storage == DISPLAY_BUFFER_STORAGE_DMA) {
        return MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    }

#ifdef CONFIG_SPIRAM
    if (byte_length > DISPLAY_BUFFER_SMALL_INTERNAL_LIMIT && esp_psram_is_initialized()) {
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    }
#else
    (void)byte_length;
#endif
    return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
}

static uint8_t *alloc_export_bytes(size_t length)
{
    uint8_t *data = heap_caps_malloc(length, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);

    if (data != NULL) {
        return data;
    }
    return heap_caps_malloc(length, MALLOC_CAP_8BIT);
}

static uint8_t *ensure_export_chunk(esp32_mquickjs_display_buffer_t *buffer, size_t length)
{
    size_t capacity = length == 0 ? 1U : length;
    uint8_t *chunk;

    if (buffer->chunk != NULL && buffer->chunk_capacity >= capacity) {
        return buffer->chunk;
    }

    chunk = alloc_export_bytes(capacity);
    if (chunk == NULL) {
        return NULL;
    }
    heap_caps_free(buffer->chunk);
    buffer->chunk = chunk;
    buffer->chunk_capacity = capacity;
    return buffer->chunk;
}

esp32_mquickjs_display_buffer_t *display_buffer_from_value(JSContext *ctx,
                                                                  JSValue value,
                                                                  const char *api_name)
{
    esp32_mquickjs_display_buffer_t *buffer;

    if (JS_GetClassID(ctx, value) != JS_CLASS_DISPLAY_BUFFER) {
        JS_ThrowTypeError(ctx, "%s expects a DisplayBuffer", api_name);
        return NULL;
    }
    buffer = JS_GetOpaque(ctx, value);
    if (buffer == NULL || buffer->closed) {
        JS_ThrowReferenceError(ctx, "%s failed because the DisplayBuffer is closed", api_name);
        return NULL;
    }
    return buffer;
}

esp32_mquickjs_display_buffer_t *esp32_mquickjs_display_buffer_from_value(JSContext *ctx,
                                                                          JSValue value,
                                                                          const char *api_name)
{
    return display_buffer_from_value(ctx, value, api_name);
}

static void clear_dirty(esp32_mquickjs_display_buffer_t *buffer)
{
    buffer->dirty_x0 = 0;
    buffer->dirty_y0 = 0;
    buffer->dirty_x1 = 0;
    buffer->dirty_y1 = 0;
}

void mark_dirty(esp32_mquickjs_display_buffer_t *buffer, int x, int y, int width, int height)
{
    int x1;
    int y1;

    if (buffer == NULL || width <= 0 || height <= 0) {
        return;
    }
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= buffer->width || y >= buffer->height || width <= 0 || height <= 0) {
        return;
    }
    if (x + width > buffer->width) {
        width = buffer->width - x;
    }
    if (y + height > buffer->height) {
        height = buffer->height - y;
    }
    x1 = x + width;
    y1 = y + height;

    if (buffer->dirty_x1 <= buffer->dirty_x0 || buffer->dirty_y1 <= buffer->dirty_y0) {
        buffer->dirty_x0 = x;
        buffer->dirty_y0 = y;
        buffer->dirty_x1 = x1;
        buffer->dirty_y1 = y1;
        return;
    }
    if (x < buffer->dirty_x0) {
        buffer->dirty_x0 = x;
    }
    if (y < buffer->dirty_y0) {
        buffer->dirty_y0 = y;
    }
    if (x1 > buffer->dirty_x1) {
        buffer->dirty_x1 = x1;
    }
    if (y1 > buffer->dirty_y1) {
        buffer->dirty_y1 = y1;
    }
}

static size_t pixel_offset(const esp32_mquickjs_display_buffer_t *buffer, int x, int y)
{
    if (buffer->format == DISPLAY_BUFFER_FORMAT_RGB565) {
        return (size_t)y * buffer->stride + ((size_t)x * 2U);
    }
    if (buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8) {
        return ((size_t)y >> 3U) * buffer->stride + (size_t)x;
    }
    return (size_t)y * buffer->stride + ((size_t)x >> 3U);
}

void set_pixel_raw(esp32_mquickjs_display_buffer_t *buffer, int x, int y, uint16_t color)
{
    size_t offset;
    uint8_t mask;

    if (x < 0 || y < 0 || x >= buffer->width || y >= buffer->height) {
        return;
    }

    offset = pixel_offset(buffer, x, y);
    if (buffer->format == DISPLAY_BUFFER_FORMAT_RGB565) {
        buffer->data[offset] = (uint8_t)((color >> 8U) & 0xffU);
        buffer->data[offset + 1U] = (uint8_t)(color & 0xffU);
        return;
    }

    mask = buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8 ? (uint8_t)(1U << (y & 7))
                                                           : (uint8_t)(1U << (x & 7));
    if (color != 0) {
        buffer->data[offset] |= mask;
    } else {
        buffer->data[offset] &= (uint8_t)~mask;
    }
}

static void fill_rgb565_bytes(uint8_t *dst, size_t pixels, uint16_t color)
{
    size_t byte_length = pixels * 2U;
    size_t filled;
    uint8_t high;
    uint8_t low;

    if (pixels == 0) {
        return;
    }
    high = (uint8_t)((color >> 8U) & 0xffU);
    low = (uint8_t)(color & 0xffU);
    if (high == low) {
        memset(dst, high, byte_length);
        return;
    }

    dst[0] = high;
    dst[1] = low;
    filled = 2U;
    while (filled < byte_length) {
        size_t copy_length = filled;
        size_t remaining = byte_length - filled;

        if (copy_length > remaining) {
            copy_length = remaining;
        }
        memcpy(dst + filled, dst, copy_length);
        filled += copy_length;
    }
}

static void fill_rgb565_span_raw(esp32_mquickjs_display_buffer_t *buffer,
                                 int x,
                                 int y,
                                 int width,
                                 uint16_t color)
{
    fill_rgb565_bytes(buffer->data + pixel_offset(buffer, x, y), (size_t)width, color);
}

static uint16_t get_pixel_raw(const esp32_mquickjs_display_buffer_t *buffer, int x, int y)
{
    size_t offset;
    uint8_t mask;

    if (x < 0 || y < 0 || x >= buffer->width || y >= buffer->height) {
        return 0;
    }

    offset = pixel_offset(buffer, x, y);
    if (buffer->format == DISPLAY_BUFFER_FORMAT_RGB565) {
        return (uint16_t)(((uint16_t)buffer->data[offset] << 8U) | buffer->data[offset + 1U]);
    }

    mask = buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8 ? (uint8_t)(1U << (y & 7))
                                                           : (uint8_t)(1U << (x & 7));
    return (buffer->data[offset] & mask) != 0 ? 1U : 0U;
}

void fill_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
                          int x,
                          int y,
                          int width,
                          int height,
                          uint16_t color,
                          bool update_dirty)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + width;
    int y1 = y + height;
    int yy;

    if (width <= 0 || height <= 0) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > buffer->width) {
        x1 = buffer->width;
    }
    if (y1 > buffer->height) {
        y1 = buffer->height;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    if (buffer->format == DISPLAY_BUFFER_FORMAT_RGB565) {
        int clipped_width = x1 - x0;
        int clipped_height = y1 - y0;
        size_t row_bytes = (size_t)clipped_width * 2U;

        if (x0 == 0 && row_bytes == buffer->stride) {
            fill_rgb565_bytes(buffer->data + pixel_offset(buffer, x0, y0),
                              (size_t)clipped_width * (size_t)clipped_height,
                              color);
        } else {
            for (yy = y0; yy < y1; ++yy) {
                fill_rgb565_span_raw(buffer, x0, yy, clipped_width, color);
            }
        }
    } else {
        int xx;

        for (yy = y0; yy < y1; ++yy) {
            for (xx = x0; xx < x1; ++xx) {
                set_pixel_raw(buffer, xx, yy, color);
            }
        }
    }

    if (update_dirty) {
        mark_dirty(buffer, x0, y0, x1 - x0, y1 - y0);
    }
}

bool rect_from_args(JSContext *ctx,
                           int argc,
                           JSValue *argv,
                           int32_t *x,
                           int32_t *y,
                           int32_t *width,
                           int32_t *height,
                           const char *api_name)
{
    if (argc < 4 ||
        !value_to_i32(ctx, argv[0], x) ||
        !value_to_i32(ctx, argv[1], y) ||
        !value_to_i32(ctx, argv[2], width) ||
        !value_to_i32(ctx, argv[3], height)) {
        JS_ThrowTypeError(ctx, "%s expects x, y, width, and height integers", api_name);
        return false;
    }
    return true;
}

static bool rect_clamp(const esp32_mquickjs_display_buffer_t *buffer,
                       int *x,
                       int *y,
                       int *width,
                       int *height)
{
    int x1 = *x + *width;
    int y1 = *y + *height;

    if (*width <= 0 || *height <= 0) {
        return false;
    }
    if (*x < 0) {
        *x = 0;
    }
    if (*y < 0) {
        *y = 0;
    }
    if (x1 > buffer->width) {
        x1 = buffer->width;
    }
    if (y1 > buffer->height) {
        y1 = buffer->height;
    }
    *width = x1 - *x;
    *height = y1 - *y;
    return *width > 0 && *height > 0;
}

static bool read_rect_options(JSContext *ctx,
                              JSValue options,
                              bool *out_little_endian,
                              uint32_t *out_chunk_bytes,
                              bool *out_reuse)
{
    JSGCRef property_ref;
    JSValue *property;
    bool ok = true;

    *out_little_endian = false;
    if (out_chunk_bytes != NULL) {
        *out_chunk_bytes = 0;
    }
    if (out_reuse != NULL) {
        *out_reuse = false;
    }
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        JS_ThrowTypeError(ctx, "readRect options must be an object");
        return false;
    }

    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "byteOrder");
    if (JS_IsException(*property)) {
        ok = false;
        goto done;
    }
    if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        JSCStringBuf order_buf;
        const char *order = JS_ToCString(ctx, *property, &order_buf);

        if (order == NULL) {
            ok = false;
            goto done;
        }
        if (string_equals(order, "le") || string_equals(order, "rgb565le")) {
            *out_little_endian = true;
        } else if (!string_equals(order, "be") && !string_equals(order, "rgb565be")) {
            JS_ThrowTypeError(ctx, "readRect option 'byteOrder' expects 'be' or 'le'");
            ok = false;
            goto done;
        }
    }

    if (out_chunk_bytes != NULL) {
        *property = JS_GetPropertyStr(ctx, options, "chunkBytes");
        if (JS_IsException(*property)) {
            ok = false;
            goto done;
        }
        if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
            (!value_to_u32(ctx, *property, out_chunk_bytes) || *out_chunk_bytes == 0)) {
            JS_ThrowTypeError(ctx, "readRectChunks option 'chunkBytes' expects a positive integer");
            ok = false;
            goto done;
        }
    }

    if (out_reuse != NULL) {
        int reuse = 0;

        *property = JS_GetPropertyStr(ctx, options, "reuse");
        if (JS_IsException(*property)) {
            ok = false;
            goto done;
        }
        if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
            if (JS_ToInt32(ctx, &reuse, *property) != 0) {
                JS_ThrowTypeError(ctx, "readRectChunks option 'reuse' expects a boolean");
                ok = false;
                goto done;
            }
            *out_reuse = reuse != 0;
        }
    }

done:
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static bool span_source_options(JSContext *ctx,
                                JSValue options,
                                bool *out_little_endian,
                                uint32_t *out_chunk_bytes)
{
    JSGCRef property_ref;
    JSValue *property;
    bool ok = true;

    *out_little_endian = false;
    *out_chunk_bytes = 0;
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        JS_ThrowTypeError(ctx, "DisplayBuffer.createSpanSource(options?) expects an object");
        return false;
    }

    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "byteOrder");
    if (JS_IsException(*property)) {
        ok = false;
        goto done;
    }
    if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        JSCStringBuf order_buf;
        const char *order = JS_ToCString(ctx, *property, &order_buf);

        if (order == NULL) {
            ok = false;
            goto done;
        }
        if (string_equals(order, "le") || string_equals(order, "rgb565le")) {
            *out_little_endian = true;
        } else if (!string_equals(order, "be") && !string_equals(order, "rgb565be")) {
            JS_ThrowTypeError(ctx, "DisplayBuffer.createSpanSource() option 'byteOrder' expects 'be' or 'le'");
            ok = false;
            goto done;
        }
    }

    *property = JS_GetPropertyStr(ctx, options, "chunkBytes");
    if (JS_IsException(*property)) {
        ok = false;
        goto done;
    }
    if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
        (!value_to_u32(ctx, *property, out_chunk_bytes) || *out_chunk_bytes == 0)) {
        JS_ThrowTypeError(ctx, "DisplayBuffer.createSpanSource() option 'chunkBytes' expects a positive integer");
        ok = false;
        goto done;
    }

done:
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static size_t read_rect_length(const esp32_mquickjs_display_buffer_t *buffer, int width, int height)
{
    if (buffer->format == DISPLAY_BUFFER_FORMAT_RGB565) {
        return (size_t)width * (size_t)height * 2U;
    }
    if (buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8) {
        return (((size_t)height + 7U) / 8U) * (size_t)width;
    }
    return (((size_t)width + 7U) / 8U) * (size_t)height;
}

static void read_rect_fill(const esp32_mquickjs_display_buffer_t *buffer,
                           int x,
                           int y,
                           int width,
                           int height,
                           bool little_endian,
                           uint8_t *out)
{
    int yy;

    if (buffer->format == DISPLAY_BUFFER_FORMAT_RGB565) {
        size_t out_offset = 0;

        if (!little_endian) {
            size_t row_bytes = (size_t)width * 2U;

            if ((size_t)width * 2U == buffer->stride) {
                memcpy(out, buffer->data + pixel_offset(buffer, x, y), row_bytes * (size_t)height);
                return;
            }
            for (yy = y; yy < y + height; ++yy) {
                memcpy(out + out_offset, buffer->data + pixel_offset(buffer, x, yy), row_bytes);
                out_offset += row_bytes;
            }
            return;
        }

        for (yy = y; yy < y + height; ++yy) {
            int xx;

            for (xx = x; xx < x + width; ++xx) {
                uint16_t color = get_pixel_raw(buffer, xx, yy);

                if (little_endian) {
                    out[out_offset++] = (uint8_t)(color & 0xffU);
                    out[out_offset++] = (uint8_t)((color >> 8U) & 0xffU);
                } else {
                    out[out_offset++] = (uint8_t)((color >> 8U) & 0xffU);
                    out[out_offset++] = (uint8_t)(color & 0xffU);
                }
            }
        }
        return;
    }

    if (buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8) {
        size_t length = read_rect_length(buffer, width, height);
        int local_y;

        memset(out, 0, length);
        for (local_y = 0; local_y < height; ++local_y) {
            int xx;

            for (xx = 0; xx < width; ++xx) {
                if (get_pixel_raw(buffer, x + xx, y + local_y) != 0) {
                    out[((size_t)local_y >> 3U) * (size_t)width + (size_t)xx] |= (uint8_t)(1U << (local_y & 7));
                }
            }
        }
        return;
    }

    {
        size_t row_bytes = ((size_t)width + 7U) / 8U;
        size_t length = read_rect_length(buffer, width, height);
        int local_y;

        memset(out, 0, length);
        for (local_y = 0; local_y < height; ++local_y) {
            int xx;

            for (xx = 0; xx < width; ++xx) {
                if (get_pixel_raw(buffer, x + xx, y + local_y) != 0) {
                    out[(size_t)local_y * row_bytes + ((size_t)xx >> 3U)] |= (uint8_t)(1U << (xx & 7));
                }
            }
        }
    }
}

static bool direct_read_rect_data(const esp32_mquickjs_display_buffer_t *buffer,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  bool little_endian,
                                  const uint8_t **out_data,
                                  size_t *out_length)
{
    if (buffer == NULL || out_data == NULL || out_length == NULL ||
        buffer->closed || little_endian || x != 0 || width <= 0 || height <= 0) {
        return false;
    }

    if (buffer->format == DISPLAY_BUFFER_FORMAT_RGB565 &&
        buffer->layout == DISPLAY_BUFFER_LAYOUT_LINEAR) {
        size_t row_length = (size_t)width * 2U;

        if (row_length != buffer->stride) {
            return false;
        }
        *out_data = buffer->data + pixel_offset(buffer, x, y);
        *out_length = row_length * (size_t)height;
        return true;
    }

    if (buffer->format == DISPLAY_BUFFER_FORMAT_MONO1 &&
        buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8) {
        size_t page_count;

        if ((y & 7) != 0 || (height & 7) != 0 || (size_t)width != buffer->stride) {
            return false;
        }
        page_count = (size_t)height >> 3U;
        *out_data = buffer->data + pixel_offset(buffer, x, y);
        *out_length = page_count * buffer->stride;
        return true;
    }

    if (buffer->format == DISPLAY_BUFFER_FORMAT_MONO1 &&
        buffer->layout == DISPLAY_BUFFER_LAYOUT_LINEAR) {
        size_t row_length = ((size_t)width + 7U) / 8U;

        if (row_length != buffer->stride) {
            return false;
        }
        *out_data = buffer->data + pixel_offset(buffer, x, y);
        *out_length = row_length * (size_t)height;
        return true;
    }

    return false;
}

bool esp32_mquickjs_display_buffer_normalize_rect(const esp32_mquickjs_display_buffer_t *buffer,
                                                  esp32_mquickjs_display_buffer_rect_t *rect)
{
    int x;
    int y;
    int width;
    int height;

    if (buffer == NULL || rect == NULL || buffer->closed) {
        return false;
    }
    x = (int)rect->x;
    y = (int)rect->y;
    width = (int)rect->width;
    height = (int)rect->height;
    if (!rect_clamp(buffer, &x, &y, &width, &height)) {
        return false;
    }
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    return true;
}

size_t esp32_mquickjs_display_buffer_rect_length(const esp32_mquickjs_display_buffer_t *buffer,
                                                 int32_t width,
                                                 int32_t height)
{
    if (buffer == NULL || buffer->closed || width <= 0 || height <= 0) {
        return 0;
    }
    return read_rect_length(buffer, width, height);
}

size_t esp32_mquickjs_display_buffer_row_length(const esp32_mquickjs_display_buffer_t *buffer,
                                                int32_t width)
{
    return esp32_mquickjs_display_buffer_rect_length(buffer, width, 1);
}

uint32_t esp32_mquickjs_display_buffer_chunk_bytes(const esp32_mquickjs_display_buffer_t *buffer)
{
    if (buffer == NULL || buffer->closed || buffer->chunk_size == 0) {
        return DISPLAY_BUFFER_DEFAULT_CHUNK_BYTES;
    }
    return (uint32_t)buffer->chunk_size;
}

bool esp32_mquickjs_display_buffer_direct_rect(const esp32_mquickjs_display_buffer_t *buffer,
                                               const esp32_mquickjs_display_buffer_rect_t *rect,
                                               const uint8_t **out_data,
                                               size_t *out_length)
{
    if (rect == NULL) {
        return false;
    }
    return direct_read_rect_data(buffer,
                                 rect->x,
                                 rect->y,
                                 rect->width,
                                 rect->height,
                                 rect->byte_order == ESP32_MQUICKJS_DISPLAY_BUFFER_BYTE_ORDER_LE,
                                 out_data,
                                 out_length);
}

bool esp32_mquickjs_display_buffer_export_rect(const esp32_mquickjs_display_buffer_t *buffer,
                                               const esp32_mquickjs_display_buffer_rect_t *rect,
                                               uint8_t *out,
                                               size_t out_length)
{
    size_t length;

    if (buffer == NULL || rect == NULL || out == NULL || buffer->closed ||
        rect->width <= 0 || rect->height <= 0) {
        return false;
    }
    length = read_rect_length(buffer, rect->width, rect->height);
    if (length == 0 || out_length < length) {
        return false;
    }
    read_rect_fill(buffer,
                   rect->x,
                   rect->y,
                   rect->width,
                   rect->height,
                   rect->byte_order == ESP32_MQUICKJS_DISPLAY_BUFFER_BYTE_ORDER_LE,
                   out);
    return true;
}

static int display_span_source_rows_per_chunk(const esp32_mquickjs_display_buffer_t *buffer,
                                              int32_t width,
                                              uint32_t chunk_bytes)
{
    size_t row_bytes = esp32_mquickjs_display_buffer_row_length(buffer, width);
    int rows_per_chunk;

    if (row_bytes == 0) {
        return 1;
    }
    rows_per_chunk = (int)(chunk_bytes / row_bytes);
    if (rows_per_chunk < 1) {
        rows_per_chunk = 1;
    }
    if (buffer->format == DISPLAY_BUFFER_FORMAT_MONO1 && buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8) {
        rows_per_chunk *= 8;
        if (rows_per_chunk < 8) {
            rows_per_chunk = 8;
        }
    }
    return rows_per_chunk;
}

static bool display_span_source_rect_direct(const display_buffer_span_source_t *source,
                                            const esp32_mquickjs_display_buffer_rect_t *rect)
{
    const uint8_t *data;
    size_t length;

    return source != NULL &&
           source->buffer != NULL &&
           esp32_mquickjs_display_buffer_direct_rect(source->buffer, rect, &data, &length);
}

static bool display_span_source_ensure_scratch(display_buffer_span_source_t *source, size_t length)
{
    uint8_t *scratch;
    size_t capacity = length == 0 ? 1U : length;

    if (source->scratch != NULL && source->scratch_capacity >= capacity) {
        return true;
    }
    scratch = alloc_export_bytes(capacity);
    if (scratch == NULL) {
        return false;
    }
    heap_caps_free(source->scratch);
    source->scratch = scratch;
    source->scratch_capacity = capacity;
    return true;
}

static bool display_span_source_prepare_rect(JSContext *ctx,
                                             display_buffer_span_source_t *source,
                                             int32_t x,
                                             int32_t y,
                                             int32_t width,
                                             int32_t height)
{
    esp32_mquickjs_display_buffer_rect_t rect;

    if (source == NULL) {
        JS_ThrowInternalError(ctx, "DisplayBufferSpanSource.setRect() received invalid source state");
        return false;
    }
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    rect.byte_order = source->rect.byte_order;

    if (source->buffer == NULL || source->buffer->closed) {
        JS_ThrowReferenceError(ctx, "DisplayBufferSpanSource.setRect() failed because the DisplayBuffer is closed");
        return false;
    }
    if (!esp32_mquickjs_display_buffer_normalize_rect(source->buffer, &rect)) {
        source->rect.x = 0;
        source->rect.y = 0;
        source->rect.width = 0;
        source->rect.height = 0;
        source->iterating = false;
        return true;
    }
    source->rect = rect;
    source->iterating = false;

    if (!display_span_source_rect_direct(source, &source->rect)) {
        int rows = display_span_source_rows_per_chunk(source->buffer, source->rect.width, source->chunk_bytes);
        size_t length;

        if (rows > source->rect.height) {
            rows = source->rect.height;
        }
        length = esp32_mquickjs_display_buffer_rect_length(source->buffer, source->rect.width, rows);
        if (!display_span_source_ensure_scratch(source, length)) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    return true;
}

static bool display_span_source_set_rect(JSContext *ctx,
                                         void *opaque,
                                         int32_t x,
                                         int32_t y,
                                         int32_t width,
                                         int32_t height)
{
    return display_span_source_prepare_rect(ctx, opaque, x, y, width, height);
}

static bool display_span_source_next(JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out);
static void display_span_source_close(JSContext *ctx, void *opaque);

static bool display_span_source_open(JSContext *ctx,
                                     JSValue source_value,
                                     void *opaque,
                                     esp32_mquickjs_byte_span_source_t *out,
                                     JSValue *out_error)
{
    display_buffer_span_source_t *source = opaque;

    if (source == NULL || source->buffer == NULL || source->buffer->closed) {
        *out_error = JS_ThrowReferenceError(ctx, "SPIDevice.writeSource(source) failed because the DisplayBuffer is closed");
        return false;
    }
    source->owner = source_value;
    source->iter_y = source->rect.y;
    source->iter_remaining = source->rect.height;
    source->iterating = true;

    out->opaque = source;
    out->next = display_span_source_next;
    out->close = display_span_source_close;
    return true;
}

static bool display_span_source_next(JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    display_buffer_span_source_t *source = opaque;
    esp32_mquickjs_display_buffer_rect_t rect;
    const uint8_t *direct_data = NULL;
    size_t length = 0;
    int rows;

    if (source == NULL || !source->iterating || source->iter_remaining <= 0) {
        return false;
    }
    if (source->buffer == NULL || source->buffer->closed) {
        JS_ThrowReferenceError(ctx, "ByteSpanSource iteration failed because the DisplayBuffer is closed");
        return false;
    }

    rows = display_span_source_rows_per_chunk(source->buffer, source->rect.width, source->chunk_bytes);
    if (rows > source->iter_remaining) {
        rows = source->iter_remaining;
    }

    rect = source->rect;
    rect.y = source->iter_y;
    rect.height = rows;

    if (esp32_mquickjs_display_buffer_direct_rect(source->buffer, &rect, &direct_data, &length)) {
        out->data = direct_data;
        out->length = length;
        out->owner = source->owner;
        out->dma_capable = true;
    } else {
        length = esp32_mquickjs_display_buffer_rect_length(source->buffer, rect.width, rect.height);
        if (!display_span_source_ensure_scratch(source, length)) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        if (length > 0 &&
            !esp32_mquickjs_display_buffer_export_rect(source->buffer, &rect, source->scratch, source->scratch_capacity)) {
            JS_ThrowInternalError(ctx, "ByteSpanSource failed to export display buffer span");
            return false;
        }
        out->data = source->scratch;
        out->length = length;
        out->owner = source->owner;
        out->dma_capable = false;
    }

    source->iter_y += rows;
    source->iter_remaining -= rows;
    return true;
}

static void display_span_source_close(JSContext *ctx, void *opaque)
{
    display_buffer_span_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) {
        return;
    }
    source->iterating = false;
    source->owner = JS_UNDEFINED;
}

static void display_span_source_destroy(JSContext *ctx, void *opaque)
{
    display_buffer_span_source_t *source = opaque;

    (void)ctx;
    if (source == NULL) {
        return;
    }
    heap_caps_free(source->scratch);
    heap_caps_free(source);
}

static const esp32_mquickjs_byte_span_source_object_ops_t display_span_source_ops = {
    .class_id = JS_CLASS_DISPLAY_BUFFER_SPAN_SOURCE,
    .open = display_span_source_open,
    .set_rect = display_span_source_set_rect,
    .destroy = display_span_source_destroy,
};

static uint8_t *read_rect_alloc(const esp32_mquickjs_display_buffer_t *buffer,
                                int x,
                                int y,
                                int width,
                                int height,
                                bool little_endian,
                                size_t *out_length)
{
    uint8_t *out;
    size_t length;

    if (!rect_clamp(buffer, &x, &y, &width, &height)) {
        *out_length = 0;
        return NULL;
    }

    length = read_rect_length(buffer, width, height);
    out = alloc_export_bytes(length);
    if (out == NULL) {
        *out_length = 0;
        return NULL;
    }
    read_rect_fill(buffer, x, y, width, height, little_endian, out);
    *out_length = length;
    return out;
}

static JSValue make_staged_rect_byte_view(JSContext *ctx,
                                          JSValue owner,
                                          esp32_mquickjs_display_buffer_t *buffer,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          bool little_endian)
{
    uint8_t *data;
    const uint8_t *direct_data;
    size_t length = 0;
    JSValue staged_view;

    if (rect_clamp(buffer, &x, &y, &width, &height)) {
        if (direct_read_rect_data(buffer, x, y, width, height, little_endian, &direct_data, &length)) {
            return esp32_mquickjs_new_byte_view(ctx, owner, direct_data, length);
        }
        length = read_rect_length(buffer, width, height);
    }

    data = ensure_export_chunk(buffer, length);
    if (data == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    if (length > 0) {
        read_rect_fill(buffer, x, y, width, height, little_endian, data);
    }

    staged_view = JS_GetPropertyStr(ctx, owner, DISPLAY_BUFFER_STAGED_VIEW_KEY);
    if (JS_IsException(staged_view)) {
        return JS_EXCEPTION;
    }
    if (JS_GetClassID(ctx, staged_view) == JS_CLASS_BYTE_VIEW) {
        if (!esp32_mquickjs_update_byte_view(ctx, staged_view, data, length)) {
            return JS_EXCEPTION;
        }
        return staged_view;
    }

    {
        JSGCRef staged_ref;
        JSValue *staged_obj = JS_PushGCRef(ctx, &staged_ref);

        *staged_obj = esp32_mquickjs_new_byte_view(ctx, owner, data, length);
        if (JS_IsException(*staged_obj)) {
            JS_PopGCRef(ctx, &staged_ref);
            return JS_EXCEPTION;
        }
        if (JS_IsException(JS_SetPropertyStr(ctx, owner, DISPLAY_BUFFER_STAGED_VIEW_KEY, *staged_obj))) {
            JS_PopGCRef(ctx, &staged_ref);
            return JS_EXCEPTION;
        }
        return JS_PopGCRef(ctx, &staged_ref);
    }
}

static JSValue make_rect_byte_view(JSContext *ctx,
                                   JSValue owner,
                                   const esp32_mquickjs_display_buffer_t *buffer,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   bool little_endian)
{
    int clamped_x = x;
    int clamped_y = y;
    int clamped_width = width;
    int clamped_height = height;
    const uint8_t *direct_data;
    uint8_t *data;
    size_t length = 0;

    if (!rect_clamp(buffer, &clamped_x, &clamped_y, &clamped_width, &clamped_height)) {
        data = alloc_export_bytes(1);
        if (data == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
        return esp32_mquickjs_new_owned_byte_view(ctx, data, 0);
    }

    if (direct_read_rect_data(buffer,
                              clamped_x,
                              clamped_y,
                              clamped_width,
                              clamped_height,
                              little_endian,
                              &direct_data,
                              &length)) {
        return esp32_mquickjs_new_byte_view(ctx, owner, direct_data, length);
    }

    data = read_rect_alloc(buffer, clamped_x, clamped_y, clamped_width, clamped_height, little_endian, &length);
    if (data == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    return esp32_mquickjs_new_owned_byte_view(ctx, data, length);
}

static bool js_array_length(JSContext *ctx, JSValue value, uint32_t *out_length)
{
    JSGCRef length_ref;
    JSValue *length_value;
    bool ok;

    if (out_length == NULL || JS_GetClassID(ctx, value) != JS_CLASS_ARRAY) {
        return false;
    }

    length_value = JS_PushGCRef(ctx, &length_ref);
    *length_value = JS_GetPropertyStr(ctx, value, "length");
    ok = !JS_IsException(*length_value) && value_to_u32(ctx, *length_value, out_length);
    JS_PopGCRef(ctx, &length_ref);
    return ok;
}

static bool rect_chunks_are_direct(const esp32_mquickjs_display_buffer_t *buffer,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   bool little_endian)
{
    const uint8_t *data;
    size_t length;

    return direct_read_rect_data(buffer, x, y, width, height, little_endian, &data, &length);
}

static JSValue make_reused_direct_rect_chunks(JSContext *ctx,
                                              JSValue owner,
                                              const esp32_mquickjs_display_buffer_t *buffer,
                                              int x,
                                              int y,
                                              int width,
                                              int height,
                                              bool little_endian,
                                              int rows_per_chunk,
                                              uint32_t chunk_count)
{
    JSGCRef array_ref;
    JSValue *array;
    uint32_t existing_length = 0;
    bool reuse_existing = false;
    bool must_cache_array = false;
    int end_y;
    uint32_t index = 0;

    if (!rect_chunks_are_direct(buffer, x, y, width, height, little_endian)) {
        return JS_UNDEFINED;
    }

    array = JS_PushGCRef(ctx, &array_ref);
    *array = JS_GetPropertyStr(ctx, owner, DISPLAY_BUFFER_STAGED_CHUNKS_KEY);
    if (JS_IsException(*array)) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }
    reuse_existing = js_array_length(ctx, *array, &existing_length) && existing_length == chunk_count;
    if (!reuse_existing) {
        *array = JS_NewArray(ctx, 0);
        must_cache_array = true;
        if (JS_IsException(*array)) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
    }

    end_y = y + height;
    while (y < end_y) {
        int rows = rows_per_chunk;
        const uint8_t *direct_data;
        size_t length = 0;
        JSValue chunk = JS_UNDEFINED;

        if (rows > end_y - y) {
            rows = end_y - y;
        }
        if (!direct_read_rect_data(buffer, x, y, width, rows, little_endian, &direct_data, &length)) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_UNDEFINED;
        }

        if (reuse_existing) {
            JSGCRef item_ref;
            JSValue *item = JS_PushGCRef(ctx, &item_ref);

            *item = JS_GetPropertyUint32(ctx, *array, index);
            if (JS_IsException(*item)) {
                JS_PopGCRef(ctx, &item_ref);
                JS_PopGCRef(ctx, &array_ref);
                return JS_EXCEPTION;
            }
            if (JS_GetClassID(ctx, *item) == JS_CLASS_BYTE_VIEW) {
                if (!esp32_mquickjs_update_byte_view(ctx, *item, direct_data, length)) {
                    JS_PopGCRef(ctx, &item_ref);
                    JS_PopGCRef(ctx, &array_ref);
                    return JS_EXCEPTION;
                }
                JS_PopGCRef(ctx, &item_ref);
                y += rows;
                index++;
                continue;
            }
            JS_PopGCRef(ctx, &item_ref);
        }

        chunk = esp32_mquickjs_new_byte_view(ctx, owner, direct_data, length);
        if (JS_IsException(chunk) || JS_IsException(JS_SetPropertyUint32(ctx, *array, index, chunk))) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
        y += rows;
        index++;
    }

    if (must_cache_array &&
        JS_IsException(JS_SetPropertyStr(ctx, owner, DISPLAY_BUFFER_STAGED_CHUNKS_KEY, *array))) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &array_ref);
}

JSValue js_display_buffer_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "DisplayBuffer cannot be constructed directly; use displayBuffer.create(options)");
}

void js_display_buffer_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_display_buffer_t *buffer = opaque;

    (void)ctx;

    if (buffer == NULL) {
        return;
    }
    heap_caps_free(buffer->data);
    heap_caps_free(buffer->chunk);
    heap_caps_free(buffer);
}

JSValue js_display_buffer_create(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf format_buf;
    JSCStringBuf layout_buf;
    JSCStringBuf storage_buf;
    const char *format_name_value = NULL;
    const char *layout_name_value = NULL;
    const char *storage_name_value = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t page_height = 0;
    uint32_t chunk_bytes = DISPLAY_BUFFER_DEFAULT_CHUNK_BYTES;
    uint8_t format;
    uint8_t layout;
    uint8_t storage;
    uint16_t computed_stride;
    uint16_t computed_page_height;
    size_t byte_length = 0;
    uint16_t foreground;
    uint16_t background;
    uint8_t *data;
    esp32_mquickjs_display_buffer_t *buffer;
    JSGCRef object_ref;
    JSValue *object;
    bool ok;

    (void)this_val;

    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]) || JS_GetClassID(ctx, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx, "displayBuffer.create(options) expects an options object");
    }
    if (!get_u32_option(ctx, argv[0], "width", &width, true, "displayBuffer.create()") ||
        !get_u32_option(ctx, argv[0], "height", &height, true, "displayBuffer.create()") ||
        !get_string_option(ctx, argv[0], "format", &format_name_value, &format_buf, true, "displayBuffer.create()")) {
        return JS_EXCEPTION;
    }
    if (!parse_format(format_name_value, &format)) {
        return JS_ThrowTypeError(ctx, "displayBuffer.create({ format }) expects 'mono1' or 'rgb565'");
    }
    if (!get_string_option(ctx, argv[0], "layout", &layout_name_value, &layout_buf, false, "displayBuffer.create()") ||
        !get_string_option(ctx, argv[0], "storage", &storage_name_value, &storage_buf, false, "displayBuffer.create()") ||
        !get_u32_option(ctx, argv[0], "stride", &stride, false, "displayBuffer.create()") ||
        !get_u32_option(ctx, argv[0], "pageHeight", &page_height, false, "displayBuffer.create()") ||
        !get_u32_option(ctx, argv[0], "chunkBytes", &chunk_bytes, false, "displayBuffer.create()")) {
        return JS_EXCEPTION;
    }
    if (chunk_bytes == 0) {
        chunk_bytes = DISPLAY_BUFFER_DEFAULT_CHUNK_BYTES;
    }
    if (!parse_layout(layout_name_value, format, &layout)) {
        return JS_ThrowTypeError(ctx, "displayBuffer.create({ layout }) expects 'linear' or 'page-y8'");
    }
    if (!parse_storage(storage_name_value, &storage)) {
        return JS_ThrowTypeError(ctx, "displayBuffer.create({ storage }) expects 'auto', 'internal', 'psram', or 'dma'");
    }
    if (!compute_layout(width,
                        height,
                        format,
                        layout,
                        stride,
                        page_height,
                        &computed_stride,
                        &computed_page_height,
                        &byte_length)) {
        return JS_ThrowRangeError(ctx, "displayBuffer.create() received invalid dimensions, layout, pageHeight, or stride");
    }

    foreground = format == DISPLAY_BUFFER_FORMAT_MONO1 ? 1U : 0xffffU;
    background = 0U;
    {
        JSGCRef property_ref;
        JSValue *property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "foreground");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        foreground = normalize_color(ctx, format, *property, foreground, &ok);
        if (!ok) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "displayBuffer.create({ foreground }) expects a valid color");
        }
        *property = JS_GetPropertyStr(ctx, argv[0], "background");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        background = normalize_color(ctx, format, *property, background, &ok);
        if (!ok) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "displayBuffer.create({ background }) expects a valid color");
        }
        JS_PopGCRef(ctx, &property_ref);
    }

    data = heap_caps_malloc(byte_length, allocation_caps(storage, byte_length));
    if (data == NULL && storage == DISPLAY_BUFFER_STORAGE_AUTO) {
        data = heap_caps_malloc(byte_length, MALLOC_CAP_8BIT);
    }
    if (data == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    buffer = heap_caps_malloc(sizeof(*buffer), MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        heap_caps_free(data);
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->width = (uint16_t)width;
    buffer->height = (uint16_t)height;
    buffer->stride = computed_stride;
    buffer->page_height = computed_page_height;
    buffer->format = format;
    buffer->layout = layout;
    buffer->storage = storage;
    buffer->byte_length = byte_length;
    buffer->data = data;
    buffer->chunk_size = chunk_bytes;
    buffer->foreground = foreground;
    buffer->background = background;
    clear_dirty(buffer);
    fill_rect_raw(buffer, 0, 0, buffer->width, buffer->height, background, false);

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_DISPLAY_BUFFER);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        heap_caps_free(buffer->data);
        heap_caps_free(buffer);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(ctx, *object, buffer);
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_display_buffer_get_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.width");
    return buffer == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, buffer->width);
}

JSValue js_display_buffer_get_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.height");
    return buffer == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, buffer->height);
}

JSValue js_display_buffer_get_format(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.format");
    return buffer == NULL ? JS_EXCEPTION : JS_NewString(ctx, format_name(buffer->format));
}

JSValue js_display_buffer_get_layout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.layout");
    return buffer == NULL ? JS_EXCEPTION : JS_NewString(ctx, layout_name(buffer->layout));
}

JSValue js_display_buffer_get_stride(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.stride");
    return buffer == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, buffer->stride);
}

JSValue js_display_buffer_get_page_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.pageHeight");
    return buffer == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, buffer->page_height);
}

JSValue js_display_buffer_get_byte_length(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.byteLength");
    return buffer == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, (uint32_t)buffer->byte_length);
}

JSValue js_display_buffer_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_DISPLAY_BUFFER) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.close() expects a DisplayBuffer");
    }
    buffer = JS_GetOpaque(ctx, *this_val);
    if (buffer != NULL && !buffer->closed) {
        heap_caps_free(buffer->data);
        heap_caps_free(buffer->chunk);
        buffer->data = NULL;
        buffer->chunk = NULL;
        buffer->closed = 1;
    }
    return JS_TRUE;
}

JSValue js_display_buffer_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.clear()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    color = normalize_color(ctx, buffer->format, argc >= 1 ? argv[0] : JS_UNDEFINED, buffer->background, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.clear(color?) expects a valid color");
    }
    fill_rect_raw(buffer, 0, 0, buffer->width, buffer->height, color, true);
    return *this_val;
}

JSValue js_display_buffer_set_pixel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.setPixel()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.setPixel(x, y, color) expects x and y integers");
    }
    color = normalize_color(ctx, buffer->format, argv[2], buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.setPixel(x, y, color) expects a valid color");
    }
    set_pixel_raw(buffer, x, y, color);
    mark_dirty(buffer, x, y, 1, 1);
    return *this_val;
}

JSValue js_display_buffer_get_pixel(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    uint16_t color;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.getPixel()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 2 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.getPixel(x, y) expects x and y integers");
    }
    color = get_pixel_raw(buffer, x, y);
    return JS_NewUint32(ctx, color);
}

JSValue js_display_buffer_fill_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.fillRect()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayBuffer.fillRect()")) {
        return JS_EXCEPTION;
    }
    color = normalize_color(ctx, buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.fillRect(x, y, width, height, color) expects a valid color");
    }
    fill_rect_raw(buffer, x, y, width, height, color, true);
    return *this_val;
}

JSValue js_display_buffer_get_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    JSGCRef object_ref;
    JSValue *object;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.getDirty()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (buffer->dirty_x1 <= buffer->dirty_x0 || buffer->dirty_y1 <= buffer->dirty_y0) {
        return JS_NULL;
    }
    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_set_property(ctx, *object, "x", JS_NewInt32(ctx, buffer->dirty_x0)) ||
        !esp32_mquickjs_set_property(ctx, *object, "y", JS_NewInt32(ctx, buffer->dirty_y0)) ||
        !esp32_mquickjs_set_property(ctx, *object, "width", JS_NewInt32(ctx, buffer->dirty_x1 - buffer->dirty_x0)) ||
        !esp32_mquickjs_set_property(ctx, *object, "height", JS_NewInt32(ctx, buffer->dirty_y1 - buffer->dirty_y0))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_display_buffer_clear_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;

    (void)argc;
    (void)argv;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.clearDirty()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    clear_dirty(buffer);
    return *this_val;
}

JSValue js_display_buffer_mark_dirty(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.markDirty()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayBuffer.markDirty()")) {
        return JS_EXCEPTION;
    }
    mark_dirty(buffer, x, y, width, height);
    return *this_val;
}

JSValue js_display_buffer_create_span_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    display_buffer_span_source_t *source;
    bool little_endian = false;
    uint32_t chunk_bytes = 0;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.createSpanSource()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!span_source_options(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED, &little_endian, &chunk_bytes)) {
        return JS_EXCEPTION;
    }
    if (chunk_bytes == 0) {
        chunk_bytes = buffer->chunk_size > 0 ? (uint32_t)buffer->chunk_size : DISPLAY_BUFFER_DEFAULT_CHUNK_BYTES;
    }

    source = heap_caps_malloc(sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(source, 0, sizeof(*source));
    source->buffer = buffer;
    source->chunk_bytes = chunk_bytes;
    source->owner = JS_UNDEFINED;
    source->rect.byte_order = little_endian ? ESP32_MQUICKJS_DISPLAY_BUFFER_BYTE_ORDER_LE
                                            : ESP32_MQUICKJS_DISPLAY_BUFFER_BYTE_ORDER_BE;

    if (!display_span_source_prepare_rect(ctx, source, 0, 0, buffer->width, buffer->height)) {
        display_span_source_destroy(ctx, source);
        return JS_EXCEPTION;
    }
    return esp32_mquickjs_new_byte_span_source(ctx, *this_val, &display_span_source_ops, source);
}

JSValue js_display_buffer_read_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool little_endian = false;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.readRect()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayBuffer.readRect()")) {
        return JS_EXCEPTION;
    }
    if (!read_rect_options(ctx, argc >= 5 ? argv[4] : JS_UNDEFINED, &little_endian, NULL, NULL)) {
        return JS_EXCEPTION;
    }
    return make_staged_rect_byte_view(ctx, *this_val, buffer, x, y, width, height, little_endian);
}

JSValue js_display_buffer_read_rect_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool little_endian = false;
    bool reuse = false;
    uint32_t chunk_bytes = 0;
    size_t row_bytes;
    int rows_per_chunk;
    int end_y;
    uint32_t chunk_count;
    uint32_t index = 0;
    JSGCRef array_ref;
    JSValue *array;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.readRectChunks()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayBuffer.readRectChunks()")) {
        return JS_EXCEPTION;
    }
    if (!read_rect_options(ctx, argc >= 5 ? argv[4] : JS_UNDEFINED, &little_endian, &chunk_bytes, &reuse)) {
        return JS_EXCEPTION;
    }
    {
        int clamped_x = x;
        int clamped_y = y;
        int clamped_width = width;
        int clamped_height = height;

        if (!rect_clamp(buffer, &clamped_x, &clamped_y, &clamped_width, &clamped_height)) {
            return JS_NewArray(ctx, 0);
        }
        x = clamped_x;
        y = clamped_y;
        width = clamped_width;
        height = clamped_height;
    }
    if (chunk_bytes == 0) {
        chunk_bytes = buffer->chunk_size > 0 ? (uint32_t)buffer->chunk_size : DISPLAY_BUFFER_DEFAULT_CHUNK_BYTES;
    }
    row_bytes = buffer->format == DISPLAY_BUFFER_FORMAT_RGB565 ? (size_t)width * 2U
                                                               : (buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8
                                                                      ? (size_t)width
                                                                      : (((size_t)width + 7U) / 8U));
    rows_per_chunk = row_bytes == 0 ? 1 : (int)(chunk_bytes / row_bytes);
    if (rows_per_chunk < 1) {
        rows_per_chunk = 1;
    }
    if (buffer->format == DISPLAY_BUFFER_FORMAT_MONO1 && buffer->layout == DISPLAY_BUFFER_LAYOUT_PAGE_Y8) {
        rows_per_chunk *= 8;
        if (rows_per_chunk < 8) {
            rows_per_chunk = 8;
        }
    }
    chunk_count = (uint32_t)((height + rows_per_chunk - 1) / rows_per_chunk);

    if (reuse) {
        JSValue reused = make_reused_direct_rect_chunks(ctx,
                                                       *this_val,
                                                       buffer,
                                                       x,
                                                       y,
                                                       width,
                                                       height,
                                                       little_endian,
                                                       rows_per_chunk,
                                                       chunk_count);
        if (JS_IsException(reused)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(reused)) {
            return reused;
        }
    }

    array = JS_PushGCRef(ctx, &array_ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }

    end_y = y + height;
    while (y < end_y) {
        int rows = rows_per_chunk;
        JSValue chunk;

        if (rows > end_y - y) {
            rows = end_y - y;
        }
        chunk = make_rect_byte_view(ctx, *this_val, buffer, x, y, width, rows, little_endian);
        if (JS_IsException(chunk) || JS_IsException(JS_SetPropertyUint32(ctx, *array, index++, chunk))) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
        y += rows;
    }
    return JS_PopGCRef(ctx, &array_ref);
}


#endif
