#include "esp32_mquickjs_display_buffer_internal.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "utils/esp32_mquickjs_byte_source.h"

#define DISPLAY_COMMAND_BUFFER_DEFAULT_COMMANDS 192U
#define DISPLAY_COMMAND_BUFFER_DEFAULT_TEXT_BYTES 2048U
#define DISPLAY_COMMAND_BUFFER_FONTS_KEY "__esp32qjsDisplayCommandFonts"
#define DISPLAY_COMMAND_TEXT_HAS_BACKGROUND 0x01U

#define DISPLAY_COMMAND_PACKED_CLEAR_SIZE 3U
#define DISPLAY_COMMAND_PACKED_RECT_SIZE 11U
#define DISPLAY_COMMAND_PACKED_ROUND_RECT_SIZE 13U
#define DISPLAY_COMMAND_PACKED_TEXT_SIZE 17U

typedef enum {
    DISPLAY_COMMAND_CLEAR = 1,
    DISPLAY_COMMAND_FILL_RECT = 2,
    DISPLAY_COMMAND_DRAW_RECT = 3,
    DISPLAY_COMMAND_DRAW_LINE = 4,
    DISPLAY_COMMAND_DRAW_ROUND_RECT = 5,
    DISPLAY_COMMAND_FILL_ROUND_RECT = 6,
    DISPLAY_COMMAND_DRAW_TEXT = 7,
} display_command_op_t;

typedef struct {
    uint8_t op;
    uint8_t flags;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t radius;
    int16_t spacing;
    uint16_t color;
    uint16_t background;
    size_t text_offset;
    size_t text_length;
    const esp32_mquickjs_bitmap_font_t *font;
} display_command_t;

typedef struct {
    display_command_t *commands;
    size_t count;
    size_t capacity;
    size_t default_capacity;
    char *text;
    size_t text_length;
    size_t text_capacity;
    size_t default_text_capacity;
    uint8_t format;
    uint16_t foreground;
    uint16_t background;
    size_t font_ref_count;
    bool closed;
} display_command_buffer_t;

static uint32_t abs_i32_to_u32_local(int32_t value)
{
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static uint16_t packed_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static int16_t packed_i16(const uint8_t *data)
{
    return (int16_t)packed_u16(data);
}

static display_command_buffer_t *display_command_buffer_from_value(JSContext *ctx,
                                                                   JSValue value,
                                                                   const char *api_name)
{
    display_command_buffer_t *command_buffer;

    if (JS_GetClassID(ctx, value) != JS_CLASS_DISPLAY_COMMAND_BUFFER) {
        JS_ThrowTypeError(ctx, "%s expects a DisplayCommandBuffer", api_name);
        return NULL;
    }
    command_buffer = JS_GetOpaque(ctx, value);
    if (command_buffer == NULL || command_buffer->closed) {
        JS_ThrowReferenceError(ctx, "%s failed because the DisplayCommandBuffer is closed", api_name);
        return NULL;
    }
    return command_buffer;
}

static void display_command_buffer_reset_internal(JSContext *ctx, display_command_buffer_t *command_buffer)
{
    (void)ctx;
    if (command_buffer == NULL) {
        return;
    }
    command_buffer->count = 0;
    command_buffer->text_length = 0;
    command_buffer->font_ref_count = 0;
}

static void display_command_buffer_close_internal(JSContext *ctx, display_command_buffer_t *command_buffer)
{
    if (command_buffer == NULL || command_buffer->closed) {
        return;
    }
    display_command_buffer_reset_internal(ctx, command_buffer);
    heap_caps_free(command_buffer->commands);
    heap_caps_free(command_buffer->text);
    command_buffer->commands = NULL;
    command_buffer->text = NULL;
    command_buffer->capacity = 0;
    command_buffer->text_capacity = 0;
    command_buffer->closed = true;
}

static bool reserve_commands(JSContext *ctx, display_command_buffer_t *command_buffer, size_t extra)
{
    display_command_t *next;
    size_t needed;
    size_t next_capacity;

    if (extra > SIZE_MAX - command_buffer->count) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    needed = command_buffer->count + extra;
    if (needed <= command_buffer->capacity) {
        return true;
    }
    next_capacity = command_buffer->capacity > 0 ? command_buffer->capacity : command_buffer->default_capacity;
    if (next_capacity == 0) {
        next_capacity = DISPLAY_COMMAND_BUFFER_DEFAULT_COMMANDS;
    }
    while (next_capacity < needed) {
        if (next_capacity > SIZE_MAX / 2U) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(*next)) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    next = heap_caps_malloc(next_capacity * sizeof(*next), MALLOC_CAP_8BIT);
    if (next == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    if (command_buffer->commands != NULL && command_buffer->count > 0) {
        memcpy(next, command_buffer->commands, command_buffer->count * sizeof(*next));
    }
    heap_caps_free(command_buffer->commands);
    command_buffer->commands = next;
    command_buffer->capacity = next_capacity;
    return true;
}

static bool reserve_text(JSContext *ctx, display_command_buffer_t *command_buffer, size_t extra)
{
    char *next;
    size_t needed;
    size_t next_capacity;

    if (extra > SIZE_MAX - command_buffer->text_length) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    needed = command_buffer->text_length + extra;
    if (needed <= command_buffer->text_capacity) {
        return true;
    }
    next_capacity = command_buffer->text_capacity > 0
        ? command_buffer->text_capacity
        : command_buffer->default_text_capacity;
    if (next_capacity == 0) {
        next_capacity = DISPLAY_COMMAND_BUFFER_DEFAULT_TEXT_BYTES;
    }
    while (next_capacity < needed) {
        if (next_capacity > SIZE_MAX / 2U) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        next_capacity *= 2U;
    }
    next = heap_caps_malloc(next_capacity, MALLOC_CAP_8BIT);
    if (next == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    if (command_buffer->text != NULL && command_buffer->text_length > 0) {
        memcpy(next, command_buffer->text, command_buffer->text_length);
    }
    heap_caps_free(command_buffer->text);
    command_buffer->text = next;
    command_buffer->text_capacity = next_capacity;
    return true;
}

static display_command_t *append_command(JSContext *ctx, display_command_buffer_t *command_buffer)
{
    display_command_t *command;

    if (!reserve_commands(ctx, command_buffer, 1U)) {
        return NULL;
    }
    command = &command_buffer->commands[command_buffer->count++];
    memset(command, 0, sizeof(*command));
    return command;
}

static bool get_u32_option(JSContext *ctx,
                           JSValue options,
                           const char *name,
                           uint32_t *out_value,
                           const char *api_name)
{
    JSGCRef property_ref;
    JSValue *property;
    bool ok = true;

    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, name);
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
        !value_to_u32(ctx, *property, out_value)) {
        JS_ThrowTypeError(ctx, "%s option '%s' expects a non-negative integer", api_name, name);
        ok = false;
    }
    JS_PopGCRef(ctx, &property_ref);
    return ok;
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
    int class_id;

    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    class_id = JS_GetClassID(ctx, options);
    if (class_id < 0 || class_id == JS_CLASS_DISPLAY_FONT) {
        JS_ThrowTypeError(ctx, "%s options must be an object", api_name);
        return false;
    }
    return true;
}

static bool text_font_from_options(JSContext *ctx,
                                   JSValue options,
                                   const char *api_name,
                                   const esp32_mquickjs_bitmap_font_t **out_font,
                                   JSValue *out_font_value)
{
    JSGCRef property_ref;
    JSValue *property;
    esp32_mquickjs_display_font_t *font;

    *out_font = NULL;
    *out_font_value = JS_UNDEFINED;
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        JS_ThrowTypeError(ctx, "%s requires a DisplayFont", api_name);
        return false;
    }

    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "font");
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (JS_IsUndefined(*property) || JS_IsNull(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowTypeError(ctx, "%s requires a DisplayFont", api_name);
        return false;
    }
    font = display_font_from_value(ctx, *property, api_name);
    if (font != NULL) {
        *out_font = &font->font;
        *out_font_value = *property;
    }
    JS_PopGCRef(ctx, &property_ref);
    return font != NULL;
}

static bool keep_font_alive(JSContext *ctx,
                            JSValue owner,
                            display_command_buffer_t *command_buffer,
                            JSValue font_value)
{
    JSGCRef owner_ref;
    JSGCRef font_ref;
    JSGCRef fonts_ref;
    JSValue *rooted_owner;
    JSValue *rooted_font;
    JSValue *fonts;
    bool ok = true;

    rooted_owner = JS_PushGCRef(ctx, &owner_ref);
    rooted_font = JS_PushGCRef(ctx, &font_ref);
    fonts = JS_PushGCRef(ctx, &fonts_ref);
    *rooted_owner = owner;
    *rooted_font = font_value;
    *fonts = JS_GetPropertyStr(ctx, *rooted_owner, DISPLAY_COMMAND_BUFFER_FONTS_KEY);
    if (JS_IsException(*fonts)) {
        JS_PopGCRef(ctx, &fonts_ref);
        JS_PopGCRef(ctx, &font_ref);
        JS_PopGCRef(ctx, &owner_ref);
        return false;
    }
    if (JS_IsUndefined(*fonts) || JS_IsNull(*fonts) || JS_GetClassID(ctx, *fonts) != JS_CLASS_ARRAY) {
        *fonts = JS_NewArray(ctx, 0);
        if (JS_IsException(*fonts) ||
            !esp32_mquickjs_set_property_ref(ctx, rooted_owner, DISPLAY_COMMAND_BUFFER_FONTS_KEY, *fonts)) {
            JS_PopGCRef(ctx, &fonts_ref);
            JS_PopGCRef(ctx, &font_ref);
            JS_PopGCRef(ctx, &owner_ref);
            return false;
        }
    }
    if (command_buffer->font_ref_count > UINT32_MAX ||
        JS_IsException(JS_SetPropertyUint32(ctx,
                                            *fonts,
                                            (uint32_t)command_buffer->font_ref_count,
                                            *rooted_font))) {
        ok = false;
    } else {
        command_buffer->font_ref_count += 1U;
    }
    JS_PopGCRef(ctx, &fonts_ref);
    JS_PopGCRef(ctx, &font_ref);
    JS_PopGCRef(ctx, &owner_ref);
    return ok;
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

static bool packed_color_is_valid(uint8_t format, uint16_t color)
{
    if (format == DISPLAY_BUFFER_FORMAT_MONO1) {
        return color <= 1U;
    }
    return true;
}

static bool packed_skip_command(const uint8_t *data, uint8_t op)
{
    switch (op) {
    case DISPLAY_COMMAND_FILL_RECT:
    case DISPLAY_COMMAND_DRAW_RECT:
        return packed_i16(data + 5) <= 0 || packed_i16(data + 7) <= 0;
    case DISPLAY_COMMAND_DRAW_ROUND_RECT:
    case DISPLAY_COMMAND_FILL_ROUND_RECT:
        return packed_i16(data + 5) <= 0 || packed_i16(data + 7) <= 0;
    case DISPLAY_COMMAND_DRAW_TEXT:
        return packed_u16(data + 15) == 0U;
    default:
        return false;
    }
}

static bool packed_command_scan(JSContext *ctx,
                                const uint8_t *data,
                                size_t length,
                                size_t text_length,
                                uint8_t format,
                                size_t *out_count,
                                size_t *out_text_bytes,
                                bool *out_needs_font)
{
    size_t offset = 0;
    size_t count = 0;
    size_t text_bytes = 0;
    bool needs_font = false;

    while (offset < length) {
        uint8_t op = data[offset];
        size_t command_size;
        uint16_t color = 0;
        uint16_t background = 0;

        switch (op) {
        case DISPLAY_COMMAND_CLEAR:
            command_size = DISPLAY_COMMAND_PACKED_CLEAR_SIZE;
            break;
        case DISPLAY_COMMAND_FILL_RECT:
        case DISPLAY_COMMAND_DRAW_RECT:
        case DISPLAY_COMMAND_DRAW_LINE:
            command_size = DISPLAY_COMMAND_PACKED_RECT_SIZE;
            break;
        case DISPLAY_COMMAND_DRAW_ROUND_RECT:
        case DISPLAY_COMMAND_FILL_ROUND_RECT:
            command_size = DISPLAY_COMMAND_PACKED_ROUND_RECT_SIZE;
            break;
        case DISPLAY_COMMAND_DRAW_TEXT: {
            uint16_t flags;
            uint16_t text_offset;
            uint16_t command_text_length;

            command_size = DISPLAY_COMMAND_PACKED_TEXT_SIZE;
            if (length - offset < command_size) {
                JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked() found a truncated text command");
                return false;
            }
            color = packed_u16(data + offset + 5U);
            background = packed_u16(data + offset + 7U);
            flags = packed_u16(data + offset + 9U);
            text_offset = packed_u16(data + offset + 13U);
            command_text_length = packed_u16(data + offset + 15U);
            if ((flags & ~DISPLAY_COMMAND_TEXT_HAS_BACKGROUND) != 0U) {
                JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked() found unsupported text flags");
                return false;
            }
            if (command_text_length > 0U) {
                if ((size_t)text_offset > text_length ||
                    (size_t)command_text_length > text_length - (size_t)text_offset) {
                    JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked() found a text range outside options.text");
                    return false;
                }
                needs_font = true;
                if ((size_t)command_text_length > SIZE_MAX - text_bytes - 1U) {
                    JS_ThrowOutOfMemory(ctx);
                    return false;
                }
                text_bytes += (size_t)command_text_length + 1U;
            }
            break;
        }
        default:
            JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked() found an unsupported command opcode");
            return false;
        }

        if (length - offset < command_size) {
            JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked() found a truncated command");
            return false;
        }
        switch (op) {
        case DISPLAY_COMMAND_CLEAR:
            color = packed_u16(data + offset + 1U);
            break;
        case DISPLAY_COMMAND_FILL_RECT:
        case DISPLAY_COMMAND_DRAW_RECT:
        case DISPLAY_COMMAND_DRAW_LINE:
            color = packed_u16(data + offset + 9U);
            break;
        case DISPLAY_COMMAND_DRAW_ROUND_RECT:
        case DISPLAY_COMMAND_FILL_ROUND_RECT:
            color = packed_u16(data + offset + 11U);
            break;
        default:
            break;
        }
        if (!packed_color_is_valid(format, color) ||
            (op == DISPLAY_COMMAND_DRAW_TEXT &&
             !packed_color_is_valid(format, background))) {
            JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked() found an invalid packed color");
            return false;
        }
        if (!packed_skip_command(data + offset, op)) {
            count += 1U;
        }
        offset += command_size;
    }

    *out_count = count;
    *out_text_bytes = text_bytes;
    *out_needs_font = needs_font;
    return true;
}

JSValue js_display_command_buffer_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;

    return JS_ThrowTypeError(ctx, "DisplayCommandBuffer objects are created by DisplayBuffer.createCommandBuffer()");
}

void js_display_command_buffer_finalizer(JSContext *ctx, void *opaque)
{
    display_command_buffer_t *command_buffer = opaque;

    display_command_buffer_close_internal(ctx, command_buffer);
    heap_caps_free(command_buffer);
}

JSValue js_display_buffer_create_command_buffer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_display_buffer_t *buffer;
    display_command_buffer_t *command_buffer;
    uint32_t command_capacity = DISPLAY_COMMAND_BUFFER_DEFAULT_COMMANDS;
    uint32_t text_capacity = DISPLAY_COMMAND_BUFFER_DEFAULT_TEXT_BYTES;
    JSGCRef object_ref;
    JSValue *object;

    buffer = display_buffer_from_value(ctx, *this_val, "DisplayBuffer.createCommandBuffer()");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!text_options_is_object(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED, "DisplayBuffer.createCommandBuffer()") ||
        !get_u32_option(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED,
                        "commandCapacity", &command_capacity, "DisplayBuffer.createCommandBuffer()") ||
        !get_u32_option(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED,
                        "textBytes", &text_capacity, "DisplayBuffer.createCommandBuffer()")) {
        return JS_EXCEPTION;
    }
    if (command_capacity == 0) {
        command_capacity = DISPLAY_COMMAND_BUFFER_DEFAULT_COMMANDS;
    }
    if (text_capacity == 0) {
        text_capacity = DISPLAY_COMMAND_BUFFER_DEFAULT_TEXT_BYTES;
    }

    command_buffer = heap_caps_malloc(sizeof(*command_buffer), MALLOC_CAP_8BIT);
    if (command_buffer == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(command_buffer, 0, sizeof(*command_buffer));
    command_buffer->default_capacity = command_capacity;
    command_buffer->default_text_capacity = text_capacity;
    command_buffer->format = buffer->format;
    command_buffer->foreground = buffer->foreground;
    command_buffer->background = buffer->background;

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_DISPLAY_COMMAND_BUFFER);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        heap_caps_free(command_buffer);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(ctx, *object, command_buffer);
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_display_command_buffer_reset(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;

    (void)argc;
    (void)argv;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.reset()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    display_command_buffer_reset_internal(ctx, command_buffer);
    if (!esp32_mquickjs_set_property_ref(ctx, this_val, DISPLAY_COMMAND_BUFFER_FONTS_KEY, JS_UNDEFINED)) {
        return JS_EXCEPTION;
    }
    return *this_val;
}

JSValue js_display_command_buffer_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;

    (void)argc;
    (void)argv;

    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_DISPLAY_COMMAND_BUFFER) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.close() expects a DisplayCommandBuffer");
    }
    command_buffer = JS_GetOpaque(ctx, *this_val);
    display_command_buffer_close_internal(ctx, command_buffer);
    if (!esp32_mquickjs_set_property_ref(ctx, this_val, DISPLAY_COMMAND_BUFFER_FONTS_KEY, JS_UNDEFINED)) {
        return JS_EXCEPTION;
    }
    return JS_TRUE;
}

JSValue js_display_command_buffer_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    display_command_t *command;
    uint16_t color;
    bool ok;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.clear()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    color = normalize_color(ctx, command_buffer->format, argc >= 1 ? argv[0] : JS_UNDEFINED, command_buffer->background, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.clear(color?) expects a valid color");
    }
    command = append_command(ctx, command_buffer);
    if (command == NULL) {
        return JS_EXCEPTION;
    }
    command->op = DISPLAY_COMMAND_CLEAR;
    command->color = color;
    return *this_val;
}

JSValue js_display_command_buffer_fill_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    display_command_t *command;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t color;
    bool ok;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.fillRect()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayCommandBuffer.fillRect()")) {
        return JS_EXCEPTION;
    }
    if (width <= 0 || height <= 0) {
        return *this_val;
    }
    color = normalize_color(ctx, command_buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, command_buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.fillRect(x, y, width, height, color) expects a valid color");
    }
    command = append_command(ctx, command_buffer);
    if (command == NULL) {
        return JS_EXCEPTION;
    }
    command->op = DISPLAY_COMMAND_FILL_RECT;
    command->x = x;
    command->y = y;
    command->width = width;
    command->height = height;
    command->color = color;
    return *this_val;
}

JSValue js_display_command_buffer_draw_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    display_command_t *command;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t color;
    bool ok;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.drawRect()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (!rect_from_args(ctx, argc, argv, &x, &y, &width, &height, "DisplayCommandBuffer.drawRect()")) {
        return JS_EXCEPTION;
    }
    if (width <= 0 || height <= 0) {
        return *this_val;
    }
    color = normalize_color(ctx, command_buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, command_buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.drawRect(x, y, width, height, color) expects a valid color");
    }
    command = append_command(ctx, command_buffer);
    if (command == NULL) {
        return JS_EXCEPTION;
    }
    command->op = DISPLAY_COMMAND_DRAW_RECT;
    command->x = x;
    command->y = y;
    command->width = width;
    command->height = height;
    command->color = color;
    return *this_val;
}

JSValue js_display_command_buffer_draw_line(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    display_command_t *command;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    uint16_t color;
    bool ok;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.drawLine()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 4 || !value_to_i32(ctx, argv[0], &x0) || !value_to_i32(ctx, argv[1], &y0) ||
        !value_to_i32(ctx, argv[2], &x1) || !value_to_i32(ctx, argv[3], &y1)) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.drawLine(x0, y0, x1, y1, color) expects integer coordinates");
    }
    color = normalize_color(ctx, command_buffer->format, argc >= 5 ? argv[4] : JS_UNDEFINED, command_buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.drawLine(x0, y0, x1, y1, color) expects a valid color");
    }
    command = append_command(ctx, command_buffer);
    if (command == NULL) {
        return JS_EXCEPTION;
    }
    command->op = DISPLAY_COMMAND_DRAW_LINE;
    command->x = x0;
    command->y = y0;
    command->width = x1;
    command->height = y1;
    command->color = color;
    return *this_val;
}

JSValue js_display_command_buffer_draw_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    display_command_t *command;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t radius;
    uint16_t color;
    bool ok;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.drawRoundRect()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 5 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y) ||
        !value_to_i32(ctx, argv[2], &width) || !value_to_i32(ctx, argv[3], &height) ||
        !value_to_i32(ctx, argv[4], &radius)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayCommandBuffer.drawRoundRect(x, y, width, height, radius, color) expects integer coordinates");
    }
    color = normalize_color(ctx, command_buffer->format, argc >= 6 ? argv[5] : JS_UNDEFINED, command_buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.drawRoundRect(x, y, width, height, radius, color) expects a valid color");
    }
    if (width <= 0 || height <= 0) {
        return *this_val;
    }
    command = append_command(ctx, command_buffer);
    if (command == NULL) {
        return JS_EXCEPTION;
    }
    command->op = DISPLAY_COMMAND_DRAW_ROUND_RECT;
    command->x = x;
    command->y = y;
    command->width = width;
    command->height = height;
    command->radius = radius;
    command->color = color;
    return *this_val;
}

JSValue js_display_command_buffer_fill_round_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    display_command_t *command;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t radius;
    uint16_t color;
    bool ok;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.fillRoundRect()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 5 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y) ||
        !value_to_i32(ctx, argv[2], &width) || !value_to_i32(ctx, argv[3], &height) ||
        !value_to_i32(ctx, argv[4], &radius)) {
        return JS_ThrowTypeError(ctx,
                                 "DisplayCommandBuffer.fillRoundRect(x, y, width, height, radius, color) expects integer coordinates");
    }
    color = normalize_color(ctx, command_buffer->format, argc >= 6 ? argv[5] : JS_UNDEFINED, command_buffer->foreground, &ok);
    if (!ok) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.fillRoundRect(x, y, width, height, radius, color) expects a valid color");
    }
    if (width <= 0 || height <= 0) {
        return *this_val;
    }
    command = append_command(ctx, command_buffer);
    if (command == NULL) {
        return JS_EXCEPTION;
    }
    command->op = DISPLAY_COMMAND_FILL_ROUND_RECT;
    command->x = x;
    command->y = y;
    command->width = width;
    command->height = height;
    command->radius = radius;
    command->color = color;
    return *this_val;
}

JSValue js_display_command_buffer_draw_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    display_command_t *command;
    int32_t x;
    int32_t y;
    uint16_t color;
    uint16_t background;
    bool ok;
    bool has_background;
    JSCStringBuf text_buf;
    const char *text;
    size_t text_length;
    int spacing;
    const esp32_mquickjs_bitmap_font_t *font;
    JSGCRef font_value_ref;
    JSValue *font_value;
    JSGCRef property_ref;
    JSValue *property;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.drawText()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !value_to_i32(ctx, argv[0], &x) || !value_to_i32(ctx, argv[1], &y)) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.drawText(x, y, text, options?) expects x, y, and text");
    }

    font_value = JS_PushGCRef(ctx, &font_value_ref);
    *font_value = JS_UNDEFINED;
    if (!text_options_is_object(ctx, argc >= 4 ? argv[3] : JS_UNDEFINED,
                                "DisplayCommandBuffer.drawText()")) {
        goto fail;
    }
    color = command_buffer->foreground;
    if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        property = JS_PushGCRef(ctx, &property_ref);
        *property = JS_GetPropertyStr(ctx, argv[3], "color");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            goto fail;
        }
        color = normalize_color(ctx, command_buffer->format, *property, command_buffer->foreground, &ok);
        JS_PopGCRef(ctx, &property_ref);
        if (!ok) {
            JS_ThrowTypeError(ctx, "DisplayCommandBuffer.drawText() option 'color' expects a valid color");
            goto fail;
        }
    }
    spacing = text_spacing_from_options(ctx, argc >= 4 ? argv[3] : JS_UNDEFINED, 0);
    if (!text_font_from_options(ctx,
                                argc >= 4 ? argv[3] : JS_UNDEFINED,
                                "DisplayCommandBuffer.drawText() option 'font'",
                                &font,
                                font_value)) {
        goto fail;
    }
    if (!text_background_from_options(ctx,
                                      command_buffer->format,
                                      argc >= 4 ? argv[3] : JS_UNDEFINED,
                                      command_buffer->background,
                                      &background,
                                      &has_background)) {
        JS_ThrowTypeError(ctx, "DisplayCommandBuffer.drawText() option 'background' expects a valid color or null");
        goto fail;
    }
    if (!keep_font_alive(ctx, *this_val, command_buffer, *font_value)) {
        goto fail;
    }

    text = JS_ToCStringLen(ctx, &text_length, argv[2], &text_buf);
    if (text == NULL || text_length == SIZE_MAX ||
        !reserve_text(ctx, command_buffer, text_length + 1U) ||
        !reserve_commands(ctx, command_buffer, 1U)) {
        goto fail;
    }

    command = &command_buffer->commands[command_buffer->count];
    memset(command, 0, sizeof(*command));
    command->op = DISPLAY_COMMAND_DRAW_TEXT;
    command->flags = has_background ? DISPLAY_COMMAND_TEXT_HAS_BACKGROUND : 0U;
    command->x = x;
    command->y = y;
    command->spacing = (int16_t)spacing;
    command->color = color;
    command->background = background;
    command->text_offset = command_buffer->text_length;
    command->text_length = text_length;
    command->font = font;
    memcpy(command_buffer->text + command_buffer->text_length, text, text_length);
    command_buffer->text[command_buffer->text_length + text_length] = '\0';
    command_buffer->text_length += text_length + 1U;
    command_buffer->count += 1U;
    JS_PopGCRef(ctx, &font_value_ref);
    return *this_val;

fail:
    JS_PopGCRef(ctx, &font_value_ref);
    return JS_EXCEPTION;
}

JSValue js_display_command_buffer_append_packed(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    char *owned_text = NULL;
    JSValue error = JS_UNDEFINED;
    JSGCRef text_value_ref;
    JSGCRef font_value_ref;
    JSValue *text_value;
    JSValue *font_value;
    JSCStringBuf text_buf;
    const char *packed_text = "";
    size_t packed_text_length = 0;
    const esp32_mquickjs_bitmap_font_t *font = NULL;
    esp32_mquickjs_display_font_t *font_object = NULL;
    size_t command_count = 0;
    size_t text_bytes = 0;
    bool needs_font = false;
    size_t offset = 0;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.appendPacked()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 1 ||
        !esp32_mquickjs_get_byte_source(ctx,
                                        argv[0],
                                        "DisplayCommandBuffer.appendPacked(bytes)",
                                        &source,
                                        &owned,
                                        &error)) {
        return JS_IsUndefined(error)
                   ? JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked(bytes, options?) expects byte commands")
                   : error;
    }

    text_value = JS_PushGCRef(ctx, &text_value_ref);
    *text_value = JS_UNDEFINED;
    font_value = JS_PushGCRef(ctx, &font_value_ref);
    *font_value = JS_UNDEFINED;

    if (!text_options_is_object(ctx, argc >= 2 ? argv[1] : JS_UNDEFINED,
                                "DisplayCommandBuffer.appendPacked()")) {
        goto fail;
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        *text_value = JS_GetPropertyStr(ctx, argv[1], "text");
        if (JS_IsException(*text_value)) {
            goto fail;
        }
        if (!JS_IsUndefined(*text_value) && !JS_IsNull(*text_value)) {
            packed_text = JS_ToCStringLen(ctx, &packed_text_length, *text_value, &text_buf);
            if (packed_text == NULL || packed_text_length == SIZE_MAX) {
                goto fail;
            }
            owned_text = heap_caps_malloc(packed_text_length + 1U, MALLOC_CAP_8BIT);
            if (owned_text == NULL) {
                JS_ThrowOutOfMemory(ctx);
                goto fail;
            }
            memcpy(owned_text, packed_text, packed_text_length);
            owned_text[packed_text_length] = '\0';
            packed_text = owned_text;
        }
        *font_value = JS_GetPropertyStr(ctx, argv[1], "font");
        if (JS_IsException(*font_value)) {
            goto fail;
        }
        if (!JS_IsUndefined(*font_value) && !JS_IsNull(*font_value)) {
            font_object = display_font_from_value(ctx,
                                                  *font_value,
                                                  "DisplayCommandBuffer.appendPacked() option 'font'");
            if (font_object == NULL) {
                goto fail;
            }
            font = &font_object->font;
        }
    }

    if (!packed_command_scan(ctx,
                             source.data,
                             source.length,
                             packed_text_length,
                             command_buffer->format,
                             &command_count,
                             &text_bytes,
                             &needs_font)) {
        goto fail;
    }
    if (needs_font && font == NULL) {
        JS_ThrowTypeError(ctx, "DisplayCommandBuffer.appendPacked() text commands require options.font");
        goto fail;
    }
    if (command_count == 0U) {
        JS_PopGCRef(ctx, &font_value_ref);
        JS_PopGCRef(ctx, &text_value_ref);
        heap_caps_free(owned_text);
        esp32_mquickjs_release_byte_source(owned);
        return *this_val;
    }
    if (!reserve_commands(ctx, command_buffer, command_count) ||
        !reserve_text(ctx, command_buffer, text_bytes)) {
        goto fail;
    }
    if (needs_font && !keep_font_alive(ctx, *this_val, command_buffer, *font_value)) {
        goto fail;
    }

    while (offset < source.length) {
        const uint8_t *packet = source.data + offset;
        uint8_t op = packet[0];
        size_t command_size = 0;
        display_command_t *command;

        switch (op) {
        case DISPLAY_COMMAND_CLEAR:
            command_size = DISPLAY_COMMAND_PACKED_CLEAR_SIZE;
            break;
        case DISPLAY_COMMAND_FILL_RECT:
        case DISPLAY_COMMAND_DRAW_RECT:
        case DISPLAY_COMMAND_DRAW_LINE:
            command_size = DISPLAY_COMMAND_PACKED_RECT_SIZE;
            break;
        case DISPLAY_COMMAND_DRAW_ROUND_RECT:
        case DISPLAY_COMMAND_FILL_ROUND_RECT:
            command_size = DISPLAY_COMMAND_PACKED_ROUND_RECT_SIZE;
            break;
        case DISPLAY_COMMAND_DRAW_TEXT:
            command_size = DISPLAY_COMMAND_PACKED_TEXT_SIZE;
            break;
        default:
            break;
        }
        if (packed_skip_command(packet, op)) {
            offset += command_size;
            continue;
        }
        command = append_command(ctx, command_buffer);
        if (command == NULL) {
            goto fail;
        }
        command->op = op;
        switch (op) {
        case DISPLAY_COMMAND_CLEAR:
            command->color = packed_u16(packet + 1U);
            break;
        case DISPLAY_COMMAND_FILL_RECT:
        case DISPLAY_COMMAND_DRAW_RECT:
            command->x = packed_i16(packet + 1U);
            command->y = packed_i16(packet + 3U);
            command->width = packed_i16(packet + 5U);
            command->height = packed_i16(packet + 7U);
            command->color = packed_u16(packet + 9U);
            break;
        case DISPLAY_COMMAND_DRAW_LINE:
            command->x = packed_i16(packet + 1U);
            command->y = packed_i16(packet + 3U);
            command->width = packed_i16(packet + 5U);
            command->height = packed_i16(packet + 7U);
            command->color = packed_u16(packet + 9U);
            break;
        case DISPLAY_COMMAND_DRAW_ROUND_RECT:
        case DISPLAY_COMMAND_FILL_ROUND_RECT:
            command->x = packed_i16(packet + 1U);
            command->y = packed_i16(packet + 3U);
            command->width = packed_i16(packet + 5U);
            command->height = packed_i16(packet + 7U);
            command->radius = packed_i16(packet + 9U);
            command->color = packed_u16(packet + 11U);
            break;
        case DISPLAY_COMMAND_DRAW_TEXT: {
            uint16_t text_offset = packed_u16(packet + 13U);
            uint16_t text_length = packed_u16(packet + 15U);
            int16_t spacing = packed_i16(packet + 11U);

            command->x = packed_i16(packet + 1U);
            command->y = packed_i16(packet + 3U);
            command->color = packed_u16(packet + 5U);
            command->background = packed_u16(packet + 7U);
            command->flags = (uint8_t)packed_u16(packet + 9U);
            if (spacing < 0) {
                spacing = 0;
            } else if (spacing > 32) {
                spacing = 32;
            }
            command->spacing = spacing;
            command->font = font;
            command->text_offset = command_buffer->text_length;
            command->text_length = text_length;
            memcpy(command_buffer->text + command_buffer->text_length,
                   packed_text + text_offset,
                   text_length);
            command_buffer->text[command_buffer->text_length + text_length] = '\0';
            command_buffer->text_length += (size_t)text_length + 1U;
            break;
        }
        default:
            break;
        }
        offset += command_size;
    }

    JS_PopGCRef(ctx, &font_value_ref);
    JS_PopGCRef(ctx, &text_value_ref);
    heap_caps_free(owned_text);
    esp32_mquickjs_release_byte_source(owned);
    return *this_val;

fail:
    JS_PopGCRef(ctx, &font_value_ref);
    JS_PopGCRef(ctx, &text_value_ref);
    heap_caps_free(owned_text);
    esp32_mquickjs_release_byte_source(owned);
    return JS_EXCEPTION;
}

JSValue js_display_command_buffer_replay(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    esp32_mquickjs_display_buffer_t *buffer;
    size_t i;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.replay()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.replay(target) expects a DisplayBuffer target");
    }
    buffer = display_buffer_from_value(ctx, argv[0], "DisplayCommandBuffer.replay(target)");
    if (buffer == NULL) {
        return JS_EXCEPTION;
    }
    if (buffer->format != command_buffer->format) {
        return JS_ThrowTypeError(ctx, "DisplayCommandBuffer.replay(target) requires a target with the same pixel format");
    }
    for (i = 0; i < command_buffer->count; ++i) {
        display_command_t *command = &command_buffer->commands[i];

        switch (command->op) {
        case DISPLAY_COMMAND_CLEAR:
            fill_rect_raw(buffer, 0, 0, buffer->width, buffer->height, command->color, true);
            break;
        case DISPLAY_COMMAND_FILL_RECT:
            fill_rect_raw(buffer, command->x, command->y, command->width, command->height, command->color, true);
            break;
        case DISPLAY_COMMAND_DRAW_RECT:
            draw_rect_raw(buffer, command->x, command->y, command->width, command->height, command->color);
            break;
        case DISPLAY_COMMAND_DRAW_LINE: {
            int min_x = command->x < command->width ? command->x : command->width;
            int min_y = command->y < command->height ? command->y : command->height;
            int max_x = command->x > command->width ? command->x : command->width;
            int max_y = command->y > command->height ? command->y : command->height;

            draw_line_raw(buffer, command->x, command->y, command->width, command->height, command->color);
            mark_dirty(buffer, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
            break;
        }
        case DISPLAY_COMMAND_DRAW_ROUND_RECT:
            draw_round_rect_raw(buffer,
                                command->x,
                                command->y,
                                command->width,
                                command->height,
                                abs_i32_to_u32_local(command->radius),
                                command->color);
            break;
        case DISPLAY_COMMAND_FILL_ROUND_RECT:
            fill_round_rect_raw(buffer,
                                command->x,
                                command->y,
                                command->width,
                                command->height,
                                abs_i32_to_u32_local(command->radius),
                                command->color);
            break;
        case DISPLAY_COMMAND_DRAW_TEXT:
            display_buffer_draw_text_raw(buffer,
                                         command->x,
                                         command->y,
                                         command_buffer->text + command->text_offset,
                                         command->font,
                                         command->spacing,
                                         command->color,
                                         (command->flags & DISPLAY_COMMAND_TEXT_HAS_BACKGROUND) != 0,
                                         command->background);
            break;
        default:
            break;
        }
    }
    return *this_val;
}

JSValue js_display_command_buffer_stats(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    display_command_buffer_t *command_buffer;
    JSGCRef object_ref;
    JSValue *object;

    (void)argc;
    (void)argv;

    command_buffer = display_command_buffer_from_value(ctx, *this_val, "DisplayCommandBuffer.stats()");
    if (command_buffer == NULL) {
        return JS_EXCEPTION;
    }
    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    if (!esp32_mquickjs_set_property_ref(ctx, object, "count", JS_NewUint32(ctx, (uint32_t)command_buffer->count)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "capacity", JS_NewUint32(ctx, (uint32_t)command_buffer->capacity)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "textBytes", JS_NewUint32(ctx, (uint32_t)command_buffer->text_length)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "textCapacity", JS_NewUint32(ctx, (uint32_t)command_buffer->text_capacity))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

#endif
