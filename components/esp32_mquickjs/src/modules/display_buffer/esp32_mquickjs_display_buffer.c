#include "esp32_mquickjs_display_buffer.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

#include "utils/esp32_mquickjs_font.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "utils/esp32_mquickjs_fs_path.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_psram.h"

#define DISPLAY_BUFFER_MAX_DIMENSION 4096U
#define DISPLAY_BUFFER_SMALL_INTERNAL_LIMIT 8192U
#define DISPLAY_BUFFER_DEFAULT_CHUNK_BYTES 4096U
#define DISPLAY_BUFFER_STAGED_VIEW_KEY "__esp32qjsDisplayBufferStagedView"
#define DISPLAY_FONT_HEADER_SIZE 16U
#define DISPLAY_FONT_MAGIC0 'E'
#define DISPLAY_FONT_MAGIC1 'Q'
#define DISPLAY_FONT_MAGIC2 'F'
#define DISPLAY_FONT_MAGIC3 '1'
#define DISPLAY_FONT_FORMAT_BITMAP_FIXED 1U

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

typedef struct {
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
} esp32_mquickjs_display_buffer_t;

typedef struct {
    esp32_mquickjs_bitmap_font_t font;
    uint8_t *glyphs;
    char *name;
} esp32_mquickjs_display_font_t;

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static char *display_font_copy_name(const char *name)
{
    size_t length;
    char *copy;

    if (name == NULL || name[0] == '\0') {
        name = "font";
    }
    length = strlen(name);
    copy = heap_caps_malloc(length + 1, MALLOC_CAP_8BIT);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, name, length + 1);
    return copy;
}

static void display_font_free(esp32_mquickjs_display_font_t *font)
{
    if (font == NULL) {
        return;
    }
    heap_caps_free(font->glyphs);
    heap_caps_free(font->name);
    heap_caps_free(font);
}

static esp32_mquickjs_display_font_t *display_font_from_value(JSContext *ctx,
                                                              JSValue value,
                                                              const char *api_name)
{
    esp32_mquickjs_display_font_t *font;

    if (JS_GetClassID(ctx, value) != JS_CLASS_DISPLAY_FONT) {
        JS_ThrowTypeError(ctx, "%s expects a DisplayFont", api_name);
        return NULL;
    }
    font = JS_GetOpaque(ctx, value);
    if (font == NULL || font->font.glyphs == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the DisplayFont is closed", api_name);
        return NULL;
    }
    return font;
}

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

static bool value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < 0) {
        return false;
    }
    *out_value = (uint32_t)raw_value;
    return true;
}

static bool value_to_i32(JSContext *ctx, JSValue value, int32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return false;
    }
    *out_value = (int32_t)raw_value;
    return true;
}

static uint16_t normalize_color(JSContext *ctx,
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

static JSValue display_font_make(JSContext *ctx,
                                 const uint8_t *bytes,
                                 size_t length,
                                 const char *name)
{
    uint8_t first;
    uint8_t last;
    uint8_t width;
    uint8_t height;
    uint8_t bytes_per_column;
    uint8_t advance;
    uint8_t line_height;
    uint32_t glyph_length;
    size_t glyph_count;
    size_t glyph_stride;
    esp32_mquickjs_display_font_t *font = NULL;
    JSGCRef object_ref;
    JSValue *object;

    if (bytes == NULL || length < DISPLAY_FONT_HEADER_SIZE) {
        return JS_ThrowTypeError(ctx, "displayBuffer.loadFont(path) found an invalid font file");
    }
    if (bytes[0] != DISPLAY_FONT_MAGIC0 || bytes[1] != DISPLAY_FONT_MAGIC1 ||
        bytes[2] != DISPLAY_FONT_MAGIC2 || bytes[3] != DISPLAY_FONT_MAGIC3 ||
        bytes[4] != DISPLAY_FONT_FORMAT_BITMAP_FIXED || bytes[5] != 0) {
        return JS_ThrowTypeError(ctx, "displayBuffer.loadFont(path) expects EQF1 bitmap-fixed font data");
    }

    first = bytes[6];
    last = bytes[7];
    width = bytes[8];
    height = bytes[9];
    advance = bytes[10];
    line_height = bytes[11];
    glyph_length = read_u32_le(bytes + 12);
    if (last < first || width == 0 || height == 0 || height > 64 ||
        advance == 0 || line_height == 0 || line_height < height) {
        return JS_ThrowTypeError(ctx, "displayBuffer.loadFont(path) found invalid font metrics");
    }

    bytes_per_column = (uint8_t)((height + 7U) / 8U);
    glyph_count = (size_t)last - (size_t)first + 1U;
    glyph_stride = (size_t)width * bytes_per_column;
    if (glyph_stride == 0 || glyph_count > SIZE_MAX / glyph_stride ||
        glyph_length != glyph_count * glyph_stride ||
        (size_t)glyph_length > length - DISPLAY_FONT_HEADER_SIZE) {
        return JS_ThrowTypeError(ctx, "displayBuffer.loadFont(path) found invalid glyph data length");
    }

    font = heap_caps_malloc(sizeof(*font), MALLOC_CAP_8BIT);
    if (font == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(font, 0, sizeof(*font));
    font->name = display_font_copy_name(name);
    font->glyphs = heap_caps_malloc(glyph_length == 0 ? 1U : (size_t)glyph_length, MALLOC_CAP_8BIT);
    if (font->name == NULL || font->glyphs == NULL) {
        display_font_free(font);
        return JS_ThrowOutOfMemory(ctx);
    }
    memcpy(font->glyphs, bytes + DISPLAY_FONT_HEADER_SIZE, glyph_length);
    font->font.name = font->name;
    font->font.glyphs = font->glyphs;
    font->font.first = first;
    font->font.last = last;
    font->font.width = width;
    font->font.height = height;
    font->font.bytes_per_column = bytes_per_column;
    font->font.advance = advance;
    font->font.line_height = line_height;

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_DISPLAY_FONT);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        display_font_free(font);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(ctx, *object, font);
    return JS_PopGCRef(ctx, &object_ref);
}

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
static uint8_t *display_font_read_file(JSContext *ctx,
                                       const char *script_path,
                                       char *resolved_path,
                                       size_t resolved_path_size,
                                       size_t *out_length)
{
    FILE *file;
    long file_size;
    uint8_t *bytes;
    size_t read_len;

    if (!esp32_mquickjs_fs_resolve_path(ESP32_MQUICKJS_LITTLEFS_BASE_PATH,
                                        script_path,
                                        resolved_path,
                                        resolved_path_size)) {
        JS_ThrowTypeError(ctx, "displayBuffer.loadFont(path) expects a path under %s",
                          ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
        return NULL;
    }

    file = fopen(resolved_path, "rb");
    if (file == NULL) {
        JS_ThrowReferenceError(ctx,
                               "displayBuffer.loadFont(path) failed for %s (%s)",
                               resolved_path,
                               strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        JS_ThrowInternalError(ctx, "displayBuffer.loadFont(path) failed to seek %s", resolved_path);
        return NULL;
    }
    file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        JS_ThrowInternalError(ctx, "displayBuffer.loadFont(path) failed to size %s", resolved_path);
        return NULL;
    }
    if (file_size < (long)DISPLAY_FONT_HEADER_SIZE) {
        fclose(file);
        JS_ThrowTypeError(ctx, "displayBuffer.loadFont(path) found a truncated font file: %s", resolved_path);
        return NULL;
    }

    bytes = heap_caps_malloc((size_t)file_size, MALLOC_CAP_8BIT);
    if (bytes == NULL) {
        fclose(file);
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    read_len = fread(bytes, 1, (size_t)file_size, file);
    fclose(file);
    if (read_len != (size_t)file_size) {
        heap_caps_free(bytes);
        JS_ThrowInternalError(ctx, "displayBuffer.loadFont(path) failed to read %s", resolved_path);
        return NULL;
    }

    *out_length = read_len;
    return bytes;
}
#endif

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

static esp32_mquickjs_display_buffer_t *display_buffer_from_value(JSContext *ctx,
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

static void clear_dirty(esp32_mquickjs_display_buffer_t *buffer)
{
    buffer->dirty_x0 = 0;
    buffer->dirty_y0 = 0;
    buffer->dirty_x1 = 0;
    buffer->dirty_y1 = 0;
}

static void mark_dirty(esp32_mquickjs_display_buffer_t *buffer, int x, int y, int width, int height)
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

static void set_pixel_raw(esp32_mquickjs_display_buffer_t *buffer, int x, int y, uint16_t color)
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

static void fill_rect_raw(esp32_mquickjs_display_buffer_t *buffer,
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
        uint8_t high = (uint8_t)((color >> 8U) & 0xffU);
        uint8_t low = (uint8_t)(color & 0xffU);
        for (yy = y0; yy < y1; ++yy) {
            size_t offset = pixel_offset(buffer, x0, yy);
            int xx;

            for (xx = x0; xx < x1; ++xx) {
                buffer->data[offset++] = high;
                buffer->data[offset++] = low;
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

static void draw_line_raw(esp32_mquickjs_display_buffer_t *buffer,
                          int x0,
                          int y0,
                          int x1,
                          int y1,
                          uint16_t color)
{
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy_abs = y1 >= y0 ? y1 - y0 : y0 - y1;
    int dy = -dy_abs;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        int e2;

        set_pixel_raw(buffer, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = err << 1;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int text_spacing_from_options(JSContext *ctx, JSValue options, int default_spacing)
{
    JSGCRef property_ref;
    JSValue *property;
    int32_t spacing = default_spacing;

    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return default_spacing;
    }
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "spacing");
    if (!JS_IsException(*property) && !JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        if (!value_to_i32(ctx, *property, &spacing)) {
            spacing = default_spacing;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    if (spacing < 0) {
        spacing = 0;
    }
    if (spacing > 32) {
        spacing = 32;
    }
    return (int)spacing;
}

static bool text_options_is_object(JSContext *ctx, JSValue options, const char *api_name)
{
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0 || JS_GetClassID(ctx, options) == JS_CLASS_DISPLAY_FONT) {
        JS_ThrowTypeError(ctx, "%s options must be an object", api_name);
        return false;
    }
    return true;
}

static const esp32_mquickjs_bitmap_font_t *text_font_from_options(JSContext *ctx,
                                                                  JSValue options,
                                                                  const char *api_name,
                                                                  bool *ok)
{
    JSGCRef property_ref;
    JSValue *property;
    esp32_mquickjs_display_font_t *font;

    *ok = true;
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        JS_ThrowTypeError(ctx, "%s requires a DisplayFont", api_name);
        *ok = false;
        return NULL;
    }

    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "font");
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        *ok = false;
        return NULL;
    }
    if (JS_IsUndefined(*property) || JS_IsNull(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowTypeError(ctx, "%s requires a DisplayFont", api_name);
        *ok = false;
        return NULL;
    }
    font = display_font_from_value(ctx, *property, api_name);
    JS_PopGCRef(ctx, &property_ref);
    if (font == NULL) {
        *ok = false;
        return NULL;
    }
    return &font->font;
}

static bool text_background_from_options(JSContext *ctx,
                                         uint8_t format,
                                         JSValue options,
                                         uint16_t fallback,
                                         uint16_t *out_color,
                                         bool *out_has_background)
{
    JSGCRef property_ref;
    JSValue *property;
    bool ok;

    *out_color = fallback;
    *out_has_background = false;

    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }

    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "background");
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (JS_IsUndefined(*property) || JS_IsNull(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return true;
    }

    *out_color = normalize_color(ctx, format, *property, fallback, &ok);
    JS_PopGCRef(ctx, &property_ref);
    if (!ok) {
        return false;
    }
    *out_has_background = true;
    return true;
}

static bool rect_from_args(JSContext *ctx,
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

static bool read_rect_options(JSContext *ctx, JSValue options, bool *out_little_endian, uint32_t *out_chunk_bytes)
{
    JSGCRef property_ref;
    JSValue *property;
    bool ok = true;

    *out_little_endian = false;
    if (out_chunk_bytes != NULL) {
        *out_chunk_bytes = 0;
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
    size_t length = 0;
    JSValue staged_view;

    if (rect_clamp(buffer, &x, &y, &width, &height)) {
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

static JSValue make_owned_rect_byte_view(JSContext *ctx,
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
    uint8_t *data;
    size_t length = 0;

    if (!rect_clamp(buffer, &clamped_x, &clamped_y, &clamped_width, &clamped_height)) {
        data = alloc_export_bytes(1);
        if (data == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
        return esp32_mquickjs_new_owned_byte_view(ctx, data, 0);
    }

    data = read_rect_alloc(buffer, clamped_x, clamped_y, clamped_width, clamped_height, little_endian, &length);
    if (data == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    return esp32_mquickjs_new_owned_byte_view(ctx, data, length);
}

JSValue js_display_buffer_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "DisplayBuffer cannot be constructed directly; use displayBuffer.create(options)");
}

JSValue js_display_font_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "DisplayFont cannot be constructed directly; use displayBuffer.loadFont(path)");
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

void js_display_font_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    display_font_free(opaque);
}

JSValue js_display_font_get_name(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_font_t *font;

    (void)argc;
    (void)argv;
    font = display_font_from_value(ctx, *this_val, "DisplayFont.name");
    if (font == NULL) {
        return JS_EXCEPTION;
    }
    return JS_NewString(ctx, font->name != NULL ? font->name : "");
}

JSValue js_display_font_get_width(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_font_t *font;

    (void)argc;
    (void)argv;
    font = display_font_from_value(ctx, *this_val, "DisplayFont.width");
    return font == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, font->font.width);
}

JSValue js_display_font_get_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_font_t *font;

    (void)argc;
    (void)argv;
    font = display_font_from_value(ctx, *this_val, "DisplayFont.height");
    return font == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, font->font.height);
}

JSValue js_display_font_get_advance(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_font_t *font;

    (void)argc;
    (void)argv;
    font = display_font_from_value(ctx, *this_val, "DisplayFont.advance");
    return font == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, font->font.advance);
}

JSValue js_display_font_get_line_height(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_font_t *font;

    (void)argc;
    (void)argv;
    font = display_font_from_value(ctx, *this_val, "DisplayFont.lineHeight");
    return font == NULL ? JS_EXCEPTION : JS_NewUint32(ctx, font->font.line_height);
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

JSValue js_display_buffer_load_font(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;

    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "displayBuffer.loadFont(path) expects a font path");
    }

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    {
        JSCStringBuf path_buf;
        const char *path;
        char resolved_path[ESP32_MQUICKJS_MAX_SCRIPT_PATH];
        uint8_t *bytes;
        size_t length = 0;
        JSValue result;

        path = JS_ToCString(ctx, argv[0], &path_buf);
        if (path == NULL) {
            return JS_EXCEPTION;
        }
        bytes = display_font_read_file(ctx, path, resolved_path, sizeof(resolved_path), &length);
        if (bytes == NULL) {
            return JS_EXCEPTION;
        }
        result = display_font_make(ctx, bytes, length, esp32_mquickjs_fs_path_basename(resolved_path));
        heap_caps_free(bytes);
        return result;
    }
#else
    return JS_ThrowInternalError(ctx, "displayBuffer.loadFont(path) requires the fs feature");
#endif
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

JSValue js_display_buffer_draw_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t color;
    bool ok;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawRect()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayBuffer.drawRect()")) {
        return JS_EXCEPTION;
    }
    if (width <= 0 || height <= 0) {
        return *this_val;
    }
    color = normalize_color(ctx, buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawRect(x, y, width, height, color) expects a valid color");
    }
    draw_line_raw(buffer, x, y, x + width - 1, y, color);
    draw_line_raw(buffer, x, y + height - 1, x + width - 1, y + height - 1, color);
    draw_line_raw(buffer, x, y, x, y + height - 1, color);
    draw_line_raw(buffer, x + width - 1, y, x + width - 1, y + height - 1, color);
    mark_dirty(buffer, x, y, width, height);
    return *this_val;
}

JSValue js_display_buffer_draw_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    uint16_t color;
    bool ok;
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawLine()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 4 || !value_to_i32(ctx, argv[0], &x0) || !value_to_i32(ctx, argv[1], &y0) ||
        !value_to_i32(ctx, argv[2], &x1) || !value_to_i32(ctx, argv[3], &y1)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawLine(x0, y0, x1, y1, color) expects integer coordinates");
    }
    color = normalize_color(ctx, buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawLine(x0, y0, x1, y1, color) expects a valid color");
    }
    draw_line_raw(buffer, x0, y0, x1, y1, color);
    min_x = x0 < x1 ? x0 : x1;
    min_y = y0 < y1 ? y0 : y1;
    max_x = x0 > x1 ? x0 : x1;
    max_y = y0 > y1 ? y0 : y1;
    mark_dirty(buffer, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
    return *this_val;
}

JSValue js_display_buffer_draw_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    uint16_t color;
    uint16_t background;
    bool ok;
    bool has_background;
    JSCStringBuf text_buf;
    const char *text;
    int spacing;
    int cursor_x;
    int cursor_y;
    int measured_width;
    int measured_height;
    int measured_lines;
    const char *cursor;
    const esp32_mquickjs_bitmap_font_t *font;
    bool font_ok;
    JSValue options;
    JSGCRef property_ref;
    JSValue *property;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawText()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawText(x, y, text, options?) expects x, y, and text");
    }
    text = JS_ToCString(ctx, argv[2], &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    options = argc >= 4 ? argv[3] : JS_UNDEFINED;
    if (!text_options_is_object(ctx, options, "DisplayBuffer.drawText()")) {
        return JS_EXCEPTION;
    }
    color = buffer->foreground;
    if (!JS_IsUndefined(options) && !JS_IsNull(options)) {
        property = JS_PushGCRef(ctx, &property_ref);
        *property = JS_GetPropertyStr(ctx, options, "color");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        color = normalize_color(ctx, buffer->format, *property, buffer->foreground, &ok);
        JS_PopGCRef(ctx, &property_ref);
        if (!ok) {
            return JS_ThrowTypeError(ctx, "DisplayBuffer.drawText() option 'color' expects a valid color");
        }
    }
    spacing = text_spacing_from_options(ctx, options, 0);
    font = text_font_from_options(ctx,
                                  options,
                                  "DisplayBuffer.drawText() option 'font'",
                                  &font_ok);
    if (!font_ok || font == NULL) {
        return JS_EXCEPTION;
    }
    if (!text_background_from_options(ctx,
                                      buffer->format,
                                      options,
                                      buffer->background,
                                      &background,
                                      &has_background)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawText() option 'background' expects a valid color or null");
    }
    cursor_x = x;
    cursor_y = y;
    for (cursor = text; *cursor != '\0'; ++cursor) {
        const uint8_t *glyph;
        int col;

        if (*cursor == '\n') {
            cursor_x = x;
            cursor_y += font->line_height;
            continue;
        }
        if (has_background) {
            fill_rect_raw(buffer, cursor_x, cursor_y, font->advance + spacing, font->line_height, background, false);
        }
        glyph = esp32_mquickjs_bitmap_font_glyph(font, *cursor);
        for (col = 0; col < font->width; ++col) {
            int row;

            for (row = 0; row < font->height; ++row) {
                uint8_t bits = glyph[((size_t)col * font->bytes_per_column) + ((size_t)row >> 3U)];

                if ((bits & (1U << (row & 7))) != 0) {
                    set_pixel_raw(buffer, cursor_x + col, cursor_y + row, color);
                }
            }
        }
        cursor_x += font->advance + spacing;
    }
    esp32_mquickjs_bitmap_font_measure(font, text, spacing, &measured_width, &measured_height, &measured_lines);
    (void)measured_lines;
    mark_dirty(buffer, x, y, measured_width, measured_height);
    return *this_val;
}

JSValue js_display_buffer_measure_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSCStringBuf text_buf;
    const char *text;
    int spacing;
    int width;
    int height;
    int lines;
    const esp32_mquickjs_bitmap_font_t *font;
    bool font_ok;
    JSGCRef object_ref;
    JSValue *object;

    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.measureText(text, options?) expects text");
    }
    text = JS_ToCString(ctx, argv[0], &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    if (!text_options_is_object(ctx, argc >= 2 ? argv[1] : JS_UNDEFINED, "DisplayBuffer.measureText()")) {
        return JS_EXCEPTION;
    }
    spacing = text_spacing_from_options(ctx, argc >= 2 ? argv[1] : JS_UNDEFINED, 0);
    font = text_font_from_options(ctx,
                                  argc >= 2 ? argv[1] : JS_UNDEFINED,
                                  "DisplayBuffer.measureText() option 'font'",
                                  &font_ok);
    if (!font_ok || font == NULL) {
        return JS_EXCEPTION;
    }
    esp32_mquickjs_bitmap_font_measure(font, text, spacing, &width, &height, &lines);

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_set_property(ctx, *object, "width", JS_NewUint32(ctx, (uint32_t)width)) ||
        !esp32_mquickjs_set_property(ctx, *object, "height", JS_NewUint32(ctx, (uint32_t)height)) ||
        !esp32_mquickjs_set_property(ctx, *object, "lines", JS_NewUint32(ctx, (uint32_t)lines))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
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
    if (!read_rect_options(ctx, argc >= 5 ? argv[4] : JS_UNDEFINED, &little_endian, NULL)) {
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
    uint32_t chunk_bytes = 0;
    size_t row_bytes;
    int rows_per_chunk;
    int end_y;
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
    if (!read_rect_options(ctx, argc >= 5 ? argv[4] : JS_UNDEFINED, &little_endian, &chunk_bytes)) {
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
        chunk = make_owned_rect_byte_view(ctx, buffer, x, y, width, rows, little_endian);
        if (JS_IsException(chunk) || JS_IsException(JS_SetPropertyUint32(ctx, *array, index++, chunk))) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
        y += rows;
    }
    return JS_PopGCRef(ctx, &array_ref);
}

JSValue js_display_buffer_draw_bitmap(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    int32_t x;
    int32_t y;
    uint32_t width = 0;
    uint32_t height = 0;
    JSGCRef property_ref;
    JSValue *property;
    JSValue pixels = JS_UNDEFINED;
    uint32_t index = 0;
    uint16_t color;
    uint16_t background;
    bool ok;
    bool has_background = false;
    uint32_t row;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawBitmap()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y) ||
        JS_GetClassID(ctx, argv[2]) < 0) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap(x, y, bitmap, options?) expects a bitmap object");
    }
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, argv[2], "width");
    if (JS_IsException(*property) || !value_to_u32(ctx, *property, &width)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() bitmap.width must be numeric");
    }
    *property = JS_GetPropertyStr(ctx, argv[2], "height");
    if (JS_IsException(*property) || !value_to_u32(ctx, *property, &height)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() bitmap.height must be numeric");
    }
    pixels = JS_GetPropertyStr(ctx, argv[2], "pixels");
    if (JS_IsException(pixels)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_EXCEPTION;
    }
    color = buffer->foreground;
    background = buffer->background;
    if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        if (JS_GetClassID(ctx, argv[3]) < 0) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() options must be an object");
        }
        *property = JS_GetPropertyStr(ctx, argv[3], "color");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        color = normalize_color(ctx, buffer->format, *property, buffer->foreground, &ok);
        if (!ok) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() option 'color' must be valid");
        }
        *property = JS_GetPropertyStr(ctx, argv[3], "background");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
            background = normalize_color(ctx, buffer->format, *property, buffer->background, &ok);
            if (!ok) {
                JS_PopGCRef(ctx, &property_ref);
                return JS_ThrowTypeError(ctx, "DisplayBuffer.drawBitmap() option 'background' must be valid or null");
            }
            has_background = true;
        }
    }
    for (row = 0; row < height; ++row) {
        uint32_t col;

        for (col = 0; col < width; ++col) {
            uint32_t mask_pixel = 0;

            *property = JS_GetPropertyUint32(ctx, pixels, index++);
            if (JS_IsException(*property)) {
                JS_PopGCRef(ctx, &property_ref);
                return JS_EXCEPTION;
            }
            if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
                (!value_to_u32(ctx, *property, &mask_pixel) || mask_pixel != 0)) {
                set_pixel_raw(buffer, x + (int32_t)col, y + (int32_t)row, color);
            } else if (has_background) {
                set_pixel_raw(buffer, x + (int32_t)col, y + (int32_t)row, background);
            }
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    mark_dirty(buffer, x, y, (int)width, (int)height);
    return *this_val;
}

#endif
