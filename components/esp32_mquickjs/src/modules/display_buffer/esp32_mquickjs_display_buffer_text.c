#include "esp32_mquickjs_display_buffer_internal.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

#include <stdbool.h>
#include <stdint.h>

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

void display_buffer_draw_text_raw(esp32_mquickjs_display_buffer_t *buffer,
                                  int32_t x,
                                  int32_t y,
                                  const char *text,
                                  const esp32_mquickjs_bitmap_font_t *font,
                                  int spacing,
                                  uint16_t color,
                                  bool has_background,
                                  uint16_t background)
{
    int cursor_x = x;
    int cursor_y = y;
    int measured_width;
    int measured_height;
    int measured_lines;
    const char *cursor;

    if (buffer == NULL || text == NULL || font == NULL) {
        return;
    }
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
    const esp32_mquickjs_bitmap_font_t *font;
    bool font_ok;
    JSGCRef property_ref;
    JSValue *property;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.drawText()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawText(x, y, text, options?) expects x, y, and text");
    }
    if (!text_options_is_object(ctx, argc >= 4 ? argv[3] : JS_UNDEFINED,
                                "DisplayBuffer.drawText()")) {
        return JS_EXCEPTION;
    }
    color = buffer->foreground;
    if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        property = JS_PushGCRef(ctx, &property_ref);
        *property = JS_GetPropertyStr(ctx, argv[3], "color");
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
    spacing = text_spacing_from_options(ctx, argc >= 4 ? argv[3] : JS_UNDEFINED, 0);
    font = text_font_from_options(ctx,
                                  argc >= 4 ? argv[3] : JS_UNDEFINED,
                                  "DisplayBuffer.drawText() option 'font'",
                                  &font_ok);
    if (!font_ok || font == NULL) {
        return JS_EXCEPTION;
    }
    if (!text_background_from_options(ctx,
                                      buffer->format,
                                      argc >= 4 ? argv[3] : JS_UNDEFINED,
                                      buffer->background,
                                      &background,
                                      &has_background)) {
        return JS_ThrowTypeError(ctx, "DisplayBuffer.drawText() option 'background' expects a valid color or null");
    }
    text = JS_ToCString(ctx, argv[2], &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    display_buffer_draw_text_raw(buffer, x, y, text, font, spacing, color, has_background, background);
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
    text = JS_ToCString(ctx, argv[0], &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    esp32_mquickjs_bitmap_font_measure(font, text, spacing, &width, &height, &lines);

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_set_property_ref(ctx, object, "width", JS_NewUint32(ctx, (uint32_t)width)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "height", JS_NewUint32(ctx, (uint32_t)height)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "lines", JS_NewUint32(ctx, (uint32_t)lines))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}


#endif
