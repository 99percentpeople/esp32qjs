#include "esp32_mquickjs_bitmap_internal.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP

#include "utils/esp32_mquickjs_fs_path.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#define DISPLAY_FONT_HEADER_SIZE 16U
#define DISPLAY_FONT_MAGIC0 'E'
#define DISPLAY_FONT_MAGIC1 'Q'
#define DISPLAY_FONT_MAGIC2 'F'
#define DISPLAY_FONT_MAGIC3 '1'
#define DISPLAY_FONT_FORMAT_BITMAP_FIXED 1U

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
    copy = esp32_mquickjs_memory_payload_alloc(
        "bitmap.font.name", length + 1,
        ESP32_MQUICKJS_MEMORY_EXTERNAL);
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
    (void)esp32_mquickjs_memory_block_free(font->glyphs_block);
    font->glyphs_block = NULL;
    font->glyphs = NULL;
    esp32_mquickjs_memory_payload_free(font->name);
    heap_caps_free(font);
}

static void display_font_glyphs_relocated(void *opaque,
                                          void *data,
                                          size_t size)
{
    esp32_mquickjs_display_font_t *font = opaque;

    (void)size;
    if (font == NULL) {
        return;
    }
    font->glyphs = data;
    font->font.glyphs = data;
}

esp32_mquickjs_display_font_t *display_font_from_value(JSContext *ctx,
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
    void *glyphs;
    JSGCRef object_ref;
    JSValue *object;

    if (bytes == NULL || length < DISPLAY_FONT_HEADER_SIZE) {
        return JS_ThrowTypeError(ctx, "bitmap.loadFont(path) found an invalid font file");
    }
    if (bytes[0] != DISPLAY_FONT_MAGIC0 || bytes[1] != DISPLAY_FONT_MAGIC1 ||
        bytes[2] != DISPLAY_FONT_MAGIC2 || bytes[3] != DISPLAY_FONT_MAGIC3 ||
        bytes[4] != DISPLAY_FONT_FORMAT_BITMAP_FIXED || bytes[5] != 0) {
        return JS_ThrowTypeError(ctx, "bitmap.loadFont(path) expects EQF1 bitmap-fixed font data");
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
        return JS_ThrowTypeError(ctx, "bitmap.loadFont(path) found invalid font metrics");
    }

    bytes_per_column = (uint8_t)((height + 7U) / 8U);
    glyph_count = (size_t)last - (size_t)first + 1U;
    glyph_stride = (size_t)width * bytes_per_column;
    if (glyph_stride == 0 || glyph_count > SIZE_MAX / glyph_stride ||
        glyph_length != glyph_count * glyph_stride ||
        (size_t)glyph_length > length - DISPLAY_FONT_HEADER_SIZE) {
        return JS_ThrowTypeError(ctx, "bitmap.loadFont(path) found invalid glyph data length");
    }

    font = heap_caps_malloc(sizeof(*font), MALLOC_CAP_8BIT);
    if (font == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(font, 0, sizeof(*font));
    font->name = display_font_copy_name(name);
    font->glyphs_block = esp32_mquickjs_memory_block_alloc(
        "bitmap.font.glyphs",
        glyph_length == 0 ? 1U : (size_t)glyph_length,
        ESP32_MQUICKJS_MEMORY_COLD_MOVABLE,
        display_font_glyphs_relocated,
        font);
    if (font->name == NULL || font->glyphs_block == NULL) {
        display_font_free(font);
        return JS_ThrowOutOfMemory(ctx);
    }
    glyphs = esp32_mquickjs_memory_block_borrow(font->glyphs_block);
    if (glyphs == NULL) {
        display_font_free(font);
        return JS_ThrowOutOfMemory(ctx);
    }
    memcpy(glyphs, bytes + DISPLAY_FONT_HEADER_SIZE, glyph_length);
    esp32_mquickjs_memory_block_release(font->glyphs_block);
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
        JS_ThrowTypeError(ctx, "bitmap.loadFont(path) expects a path under %s",
                          ESP32_MQUICKJS_LITTLEFS_BASE_PATH);
        return NULL;
    }

    file = fopen(resolved_path, "rb");
    if (file == NULL) {
        JS_ThrowReferenceError(ctx,
                               "bitmap.loadFont(path) failed for %s (%s)",
                               resolved_path,
                               strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        JS_ThrowInternalError(ctx, "bitmap.loadFont(path) failed to seek %s", resolved_path);
        return NULL;
    }
    file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        JS_ThrowInternalError(ctx, "bitmap.loadFont(path) failed to size %s", resolved_path);
        return NULL;
    }
    if (file_size < (long)DISPLAY_FONT_HEADER_SIZE) {
        fclose(file);
        JS_ThrowTypeError(ctx, "bitmap.loadFont(path) found a truncated font file: %s", resolved_path);
        return NULL;
    }

    bytes = esp32_mquickjs_memory_payload_alloc(
        "bitmap.font.file", (size_t)file_size,
        ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (bytes == NULL) {
        fclose(file);
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    read_len = fread(bytes, 1, (size_t)file_size, file);
    fclose(file);
    if (read_len != (size_t)file_size) {
        esp32_mquickjs_memory_payload_free(bytes);
        JS_ThrowInternalError(ctx, "bitmap.loadFont(path) failed to read %s", resolved_path);
        return NULL;
    }

    *out_length = read_len;
    return bytes;
}
#endif

JSValue js_display_font_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "DisplayFont cannot be constructed directly; use bitmap.loadFont(path)");
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

JSValue js_bitmap_load_font(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;

    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "bitmap.loadFont(path) expects a font path");
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
        esp32_mquickjs_memory_payload_free(bytes);
        return result;
    }
#else
    return JS_ThrowInternalError(ctx, "bitmap.loadFont(path) requires the fs feature");
#endif
}

#endif
