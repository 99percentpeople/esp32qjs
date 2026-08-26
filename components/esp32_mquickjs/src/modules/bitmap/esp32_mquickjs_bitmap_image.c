#include "esp32_mquickjs_bitmap_internal.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP

#include "esp32_mquickjs_future.h"
#include "utils/esp32_mquickjs_byte_source.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
#include "esp32_mquickjs_camera.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "esp_heap_caps.h"

#define BITMAP_CONVERT_API "bitmap.convert()"
#define BITMAP_BLIT_API "Bitmap.blit()"

typedef enum {
    BITMAP_SOURCE_NONE = 0,
    BITMAP_SOURCE_BITMAP,
    BITMAP_SOURCE_CAMERA,
    BITMAP_SOURCE_BYTE_VIEW,
    BITMAP_SOURCE_STAGING,
} bitmap_source_owner_t;

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    JSGCRef source_ref;
    JSGCRef pixels_ref;
    JSGCRef target_ref;
    bool source_rooted;
    bool pixels_rooted;
    bool target_rooted;
    bool convert;
    bool started;
    _Atomic bool completed;
    _Atomic bool cancelled;
    bitmap_source_owner_t source_owner;
    esp32_mquickjs_bitmap_view_t source;
    esp32_mquickjs_bitmap_target_t target;
    esp32_mquickjs_bitmap_transform_options_t options;
    esp32_mquickjs_bitmap_t *source_bitmap;
    esp32_mquickjs_bitmap_t *target_bitmap;
    esp32_mquickjs_bitmap_t *result_bitmap;
#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
    esp32_mquickjs_camera_bitmap_lease_t camera_lease;
#endif
    uint8_t *staging;
    uint32_t rows_completed;
    esp32_mquickjs_bitmap_dirty_rect_t dirty;
    esp32_mquickjs_bitmap_transform_result_t result;
};

static bool value_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0 && !JS_IsArray(ctx, value);
}

static bool read_u32_property(JSContext *ctx,
                              JSValue object,
                              const char *name,
                              const char *api_name,
                              uint32_t *out,
                              bool *out_present)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool ok = true;

    *out_present = false;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) {
        ok = false;
    } else if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        if (!value_to_u32(ctx, *property, out)) {
            JS_ThrowTypeError(ctx,
                              "%s option '%s' expects a non-negative integer",
                              api_name, name);
            ok = false;
        } else {
            *out_present = true;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static bool read_i32_property(JSContext *ctx,
                              JSValue object,
                              const char *name,
                              const char *api_name,
                              int32_t *out,
                              bool *out_present)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool ok = true;

    *out_present = false;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) {
        ok = false;
    } else if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        if (!value_to_i32(ctx, *property, out)) {
            JS_ThrowTypeError(ctx, "%s option '%s' expects an integer",
                              api_name, name);
            ok = false;
        } else {
            *out_present = true;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static bool read_bool_property(JSContext *ctx,
                               JSValue object,
                               const char *name,
                               const char *api_name,
                               bool *out,
                               bool *out_present)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool ok = true;

    *out_present = false;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) {
        ok = false;
    } else if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        if (!JS_IsBool(*property)) {
            JS_ThrowTypeError(ctx, "%s option '%s' expects a boolean",
                              api_name, name);
            ok = false;
        } else {
            *out = *property == JS_TRUE;
            *out_present = true;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static bool read_string_property(JSContext *ctx,
                                 JSValue object,
                                 const char *name,
                                 const char *api_name,
                                 const char **out,
                                 JSCStringBuf *out_buf,
                                 bool *out_present)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool ok = true;

    *out = NULL;
    *out_present = false;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) {
        ok = false;
    } else if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        if (!JS_IsString(ctx, *property) ||
            (*out = JS_ToCString(ctx, *property, out_buf)) == NULL) {
            JS_ThrowTypeError(ctx, "%s option '%s' expects a string",
                              api_name, name);
            ok = false;
        } else {
            *out_present = true;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return ok;
}

static bool parse_format(const char *name,
                         esp32_mquickjs_bitmap_pixel_format_t *out)
{
    if (name != NULL && strcmp(name, "mono1") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_FORMAT_MONO1;
        return true;
    }
    if (name != NULL && strcmp(name, "gray8") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8;
        return true;
    }
    if (name != NULL && strcmp(name, "rgb565") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_FORMAT_RGB565;
        return true;
    }
    if (name != NULL && strcmp(name, "rgb888") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_FORMAT_RGB888;
        return true;
    }
    return false;
}

static bool parse_layout(const char *name,
                         esp32_mquickjs_bitmap_pixel_format_t format,
                         bool descriptor,
                         esp32_mquickjs_bitmap_layout_t *out)
{
    if (name == NULL) {
        *out = format == ESP32_MQUICKJS_BITMAP_FORMAT_MONO1 && !descriptor
                   ? ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8
                   : ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR;
        return true;
    }
    if (strcmp(name, "linear") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_LAYOUT_LINEAR;
        return true;
    }
    if (strcmp(name, "page-y8") == 0 &&
        format == ESP32_MQUICKJS_BITMAP_FORMAT_MONO1) {
        *out = ESP32_MQUICKJS_BITMAP_LAYOUT_PAGE_Y8;
        return true;
    }
    return false;
}

static bool parse_byte_order(const char *name,
                             esp32_mquickjs_bitmap_byte_order_t *out)
{
    if (name == NULL || strcmp(name, "be") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE;
        return true;
    }
    if (strcmp(name, "le") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_LE;
        return true;
    }
    return false;
}

static bool parse_bit_order(const char *name,
                            esp32_mquickjs_bitmap_bit_order_t *out)
{
    if (name == NULL || strcmp(name, "lsb") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB;
        return true;
    }
    if (strcmp(name, "msb") == 0) {
        *out = ESP32_MQUICKJS_BITMAP_BIT_ORDER_MSB;
        return true;
    }
    return false;
}

static bool parse_storage(const char *name, uint8_t *out)
{
    if (name == NULL || strcmp(name, "auto") == 0) {
        *out = BITMAP_STORAGE_AUTO;
        return true;
    }
    if (strcmp(name, "internal") == 0) {
        *out = BITMAP_STORAGE_INTERNAL;
        return true;
    }
    if (strcmp(name, "psram") == 0) {
        *out = BITMAP_STORAGE_PSRAM;
        return true;
    }
    if (strcmp(name, "dma") == 0) {
        *out = BITMAP_STORAGE_DMA;
        return true;
    }
    return false;
}

static bool root_value(JSContext *ctx,
                       JSGCRef *ref,
                       bool *rooted,
                       JSValue value)
{
    JSValue *slot = JS_AddGCRef(ctx, ref);

    if (slot == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    *slot = value;
    *rooted = true;
    return true;
}

static bool source_from_bitmap(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_bitmap_t *buffer =
        bitmap_from_value(ctx, value, api_name);

    if (buffer == NULL || !bitmap_acquire_read(ctx, buffer, api_name) ||
        !root_value(ctx, &state->source_ref, &state->source_rooted, value)) {
        if (buffer != NULL && buffer->read_leases != 0) {
            bitmap_release_read(buffer);
        }
        return false;
    }
    state->source_owner = BITMAP_SOURCE_BITMAP;
    state->source_bitmap = buffer;
    state->source.data = buffer->data;
    state->source.length = buffer->byte_length;
    state->source.width = buffer->width;
    state->source.height = buffer->height;
    state->source.stride = buffer->stride;
    state->source.format =
        (esp32_mquickjs_bitmap_pixel_format_t)buffer->format;
    state->source.layout = (esp32_mquickjs_bitmap_layout_t)buffer->layout;
    state->source.byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE;
    state->source.bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB;
    return true;
}

static bool source_from_descriptor(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_future_driver_state_t *state)
{
    JSCStringBuf format_buf;
    JSCStringBuf layout_buf;
    JSCStringBuf byte_order_buf;
    JSCStringBuf bit_order_buf;
    const char *format_name = NULL;
    const char *layout_name = NULL;
    const char *byte_order_name = NULL;
    const char *bit_order_name = NULL;
    bool present;
    bool format_present;
    bool layout_present;
    bool byte_order_present;
    bool bit_order_present;
    uint32_t computed_stride;
    size_t required;
    JSGCRef pixels_local_ref;
    JSValue *pixels = JS_PushGCRef(ctx, &pixels_local_ref);
    JSValue error = JS_UNDEFINED;
    esp32_mquickjs_byte_source_t bytes;

    if (!value_is_object(ctx, value)) {
        JS_PopGCRef(ctx, &pixels_local_ref);
        JS_ThrowTypeError(
            ctx,
            "%s source expects a Bitmap, raw CameraFrame, or pixel descriptor",
            api_name);
        return false;
    }
    if (!read_u32_property(ctx, value, "width", api_name,
                           &state->source.width, &present) ||
        !present ||
        !read_u32_property(ctx, value, "height", api_name,
                           &state->source.height, &present) ||
        !present ||
        !read_string_property(ctx, value, "format", api_name, &format_name,
                              &format_buf, &format_present) ||
        !format_present ||
        !read_string_property(ctx, value, "layout", api_name, &layout_name,
                              &layout_buf, &layout_present) ||
        !read_string_property(ctx, value, "byteOrder", api_name,
                              &byte_order_name, &byte_order_buf,
                              &byte_order_present) ||
        !read_string_property(ctx, value, "bitOrder", api_name,
                              &bit_order_name, &bit_order_buf,
                              &bit_order_present)) {
        JS_PopGCRef(ctx, &pixels_local_ref);
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "%s descriptor requires width, height, and format",
                              api_name);
        }
        return false;
    }
    if (state->source.width == 0 || state->source.height == 0 ||
        state->source.width > BITMAP_MAX_DIMENSION ||
        state->source.height > BITMAP_MAX_DIMENSION ||
        !parse_format(format_name, &state->source.format) ||
        !parse_layout(layout_present ? layout_name : NULL,
                      state->source.format, true, &state->source.layout) ||
        !parse_byte_order(byte_order_present ? byte_order_name : NULL,
                          &state->source.byte_order) ||
        !parse_bit_order(bit_order_present ? bit_order_name : NULL,
                         &state->source.bit_order) ||
        (byte_order_present &&
         state->source.format != ESP32_MQUICKJS_BITMAP_FORMAT_RGB565) ||
        (bit_order_present &&
         state->source.format != ESP32_MQUICKJS_BITMAP_FORMAT_MONO1)) {
        JS_PopGCRef(ctx, &pixels_local_ref);
        JS_ThrowTypeError(
            ctx,
            "%s descriptor has an invalid format, layout, byteOrder, or bitOrder",
            api_name);
        return false;
    }
    state->source.stride = 0;
    if (!read_u32_property(ctx, value, "stride", api_name,
                           &state->source.stride, &present) ||
        !esp32_mquickjs_bitmap_compute_storage(
            state->source.width, state->source.height, state->source.format,
            state->source.layout, state->source.stride, &computed_stride,
            &required)) {
        JS_PopGCRef(ctx, &pixels_local_ref);
        if (!JS_HasException(ctx)) {
            JS_ThrowRangeError(ctx, "%s descriptor has invalid dimensions or stride",
                               api_name);
        }
        return false;
    }
    state->source.stride = computed_stride;
    *pixels = JS_GetPropertyStr(ctx, value, "pixels");
    if (JS_IsException(*pixels) || JS_IsUndefined(*pixels) || JS_IsNull(*pixels) ||
        !root_value(ctx, &state->pixels_ref, &state->pixels_rooted, *pixels)) {
        JS_PopGCRef(ctx, &pixels_local_ref);
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "%s descriptor requires pixel bytes",
                              api_name);
        }
        return false;
    }
    if (JS_GetClassID(ctx, *pixels) == JS_CLASS_BYTE_VIEW) {
        if (!esp32_mquickjs_byte_view_acquire_read(
                ctx, *pixels, api_name, &state->source.data,
                &state->source.length)) {
            JS_PopGCRef(ctx, &pixels_local_ref);
            return false;
        }
        state->source_owner = BITMAP_SOURCE_BYTE_VIEW;
    } else if (!esp32_mquickjs_get_byte_source(
                   ctx, *pixels, api_name, &bytes, &state->staging, &error)) {
        JS_PopGCRef(ctx, &pixels_local_ref);
        if (!JS_IsUndefined(error) && !JS_IsException(error)) {
            (void)JS_Throw(ctx, error);
        }
        return false;
    } else {
        state->source.data = bytes.data;
        state->source.length = bytes.length;
        state->source_owner = BITMAP_SOURCE_STAGING;
    }
    JS_PopGCRef(ctx, &pixels_local_ref);
    if (state->source.length < required) {
        JS_ThrowRangeError(ctx, "%s descriptor pixel data is truncated",
                           api_name);
        return false;
    }
    return true;
}

static bool source_from_value(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (JS_GetClassID(ctx, value) == JS_CLASS_BITMAP) {
        return source_from_bitmap(ctx, value, api_name, state);
    }
#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
    if (JS_GetClassID(ctx, value) == JS_CLASS_CAMERA_FRAME) {
        if (!esp32_mquickjs_camera_frame_acquire_bitmap_view(
                ctx, value, api_name, &state->camera_lease) ||
            !root_value(ctx, &state->source_ref, &state->source_rooted,
                        value)) {
            esp32_mquickjs_camera_frame_release_bitmap_view(
                &state->camera_lease);
            return false;
        }
        state->source_owner = BITMAP_SOURCE_CAMERA;
        state->source = state->camera_lease.view;
        return true;
    }
#endif
    return source_from_descriptor(ctx, value, api_name, state);
}

static bool parse_source_rect(
    JSContext *ctx,
    JSValue options,
    const char *api_name,
    const esp32_mquickjs_bitmap_view_t *source,
    esp32_mquickjs_bitmap_transform_options_t *out)
{
    JSGCRef rect_ref;
    JSValue *rect = JS_PushGCRef(ctx, &rect_ref);
    bool present;
    bool value_present;

    out->source_x = 0;
    out->source_y = 0;
    out->source_width = source->width;
    out->source_height = source->height;
    *rect = JS_GetPropertyStr(ctx, options, "sourceRect");
    if (JS_IsException(*rect)) {
        JS_PopGCRef(ctx, &rect_ref);
        return false;
    }
    if (JS_IsUndefined(*rect) || JS_IsNull(*rect)) {
        JS_PopGCRef(ctx, &rect_ref);
        return true;
    }
    if (!value_is_object(ctx, *rect) ||
        !read_u32_property(ctx, *rect, "x", api_name, &out->source_x,
                           &present) ||
        !read_u32_property(ctx, *rect, "y", api_name, &out->source_y,
                           &present) ||
        !read_u32_property(ctx, *rect, "width", api_name,
                           &out->source_width, &value_present) ||
        !value_present ||
        !read_u32_property(ctx, *rect, "height", api_name,
                           &out->source_height, &value_present) ||
        !value_present || out->source_width == 0 || out->source_height == 0 ||
        out->source_x >= source->width || out->source_y >= source->height ||
        out->source_width > source->width - out->source_x ||
        out->source_height > source->height - out->source_y) {
        JS_PopGCRef(ctx, &rect_ref);
        if (!JS_HasException(ctx)) {
            JS_ThrowRangeError(ctx, "%s sourceRect is outside the source",
                               api_name);
        }
        return false;
    }
    JS_PopGCRef(ctx, &rect_ref);
    return true;
}

static bool parse_destination_rect(
    JSContext *ctx,
    JSValue options,
    const char *api_name,
    uint32_t natural_width,
    uint32_t natural_height,
    esp32_mquickjs_bitmap_transform_options_t *out)
{
    JSGCRef rect_ref;
    JSValue *rect = JS_PushGCRef(ctx, &rect_ref);
    bool present;
    bool size_present;

    out->destination_x = 0;
    out->destination_y = 0;
    out->destination_width = natural_width;
    out->destination_height = natural_height;
    *rect = JS_GetPropertyStr(ctx, options, "destinationRect");
    if (JS_IsException(*rect)) {
        JS_PopGCRef(ctx, &rect_ref);
        return false;
    }
    if (JS_IsUndefined(*rect) || JS_IsNull(*rect)) {
        JS_PopGCRef(ctx, &rect_ref);
        return true;
    }
    if (!value_is_object(ctx, *rect) ||
        !read_i32_property(ctx, *rect, "x", api_name, &out->destination_x,
                           &present) ||
        !read_i32_property(ctx, *rect, "y", api_name, &out->destination_y,
                           &present) ||
        !read_u32_property(ctx, *rect, "width", api_name,
                           &out->destination_width, &size_present) ||
        !size_present ||
        !read_u32_property(ctx, *rect, "height", api_name,
                           &out->destination_height, &size_present) ||
        !size_present || out->destination_width == 0 ||
        out->destination_height == 0) {
        JS_PopGCRef(ctx, &rect_ref);
        if (!JS_HasException(ctx)) {
            JS_ThrowRangeError(ctx, "%s destinationRect requires positive dimensions",
                               api_name);
        }
        return false;
    }
    JS_PopGCRef(ctx, &rect_ref);
    return true;
}

static bool parse_transform_options(
    JSContext *ctx,
    JSValue options,
    const char *api_name,
    const esp32_mquickjs_bitmap_view_t *source,
    esp32_mquickjs_bitmap_pixel_format_t target_format,
    bool convert,
    esp32_mquickjs_bitmap_transform_options_t *out,
    uint32_t *out_convert_width,
    uint32_t *out_convert_height)
{
    JSCStringBuf filter_buf;
    JSCStringBuf dither_buf;
    const char *filter = NULL;
    const char *dither = NULL;
    bool present;
    bool threshold_present;
    bool dither_present;
    uint32_t rotation = 0;
    uint32_t threshold = 128;
    uint32_t natural_width;
    uint32_t natural_height;

    memset(out, 0, sizeof(*out));
    out->filter = ESP32_MQUICKJS_BITMAP_FILTER_NEAREST;
    out->dither = ESP32_MQUICKJS_BITMAP_DITHER_NONE;
    out->threshold = 128;
    if (!parse_source_rect(ctx, options, api_name, source, out) ||
        !read_u32_property(ctx, options, "rotation", api_name, &rotation,
                           &present) ||
        (rotation != 0 && rotation != 90 && rotation != 180 &&
         rotation != 270) ||
        !read_bool_property(ctx, options, "flipX", api_name, &out->flip_x,
                            &present) ||
        !read_bool_property(ctx, options, "flipY", api_name, &out->flip_y,
                            &present) ||
        !read_bool_property(ctx, options, "normalize", api_name,
                            &out->normalize, &present) ||
        !read_string_property(ctx, options, "filter", api_name, &filter,
                              &filter_buf, &present) ||
        !read_string_property(ctx, options, "dither", api_name, &dither,
                              &dither_buf, &dither_present) ||
        !read_u32_property(ctx, options, "threshold", api_name, &threshold,
                           &threshold_present)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "%s received invalid transform options",
                              api_name);
        }
        return false;
    }
    out->rotation = (uint16_t)rotation;
    if (filter != NULL && strcmp(filter, "nearest") == 0) {
        out->filter = ESP32_MQUICKJS_BITMAP_FILTER_NEAREST;
    } else if (filter != NULL && strcmp(filter, "bilinear") == 0) {
        out->filter = ESP32_MQUICKJS_BITMAP_FILTER_BILINEAR;
    } else if (filter != NULL) {
        JS_ThrowTypeError(ctx, "%s filter expects 'nearest' or 'bilinear'",
                          api_name);
        return false;
    }
    if (dither != NULL && strcmp(dither, "none") == 0) {
        out->dither = ESP32_MQUICKJS_BITMAP_DITHER_NONE;
    } else if (dither != NULL && strcmp(dither, "bayer4x4") == 0) {
        out->dither = ESP32_MQUICKJS_BITMAP_DITHER_BAYER_4X4;
    } else if (dither != NULL) {
        JS_ThrowTypeError(ctx, "%s dither expects 'none' or 'bayer4x4'",
                          api_name);
        return false;
    }
    if (threshold > 255U ||
        ((threshold_present || dither_present) &&
         target_format != ESP32_MQUICKJS_BITMAP_FORMAT_MONO1) ||
        (out->normalize &&
         target_format != ESP32_MQUICKJS_BITMAP_FORMAT_MONO1 &&
         target_format != ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8)) {
        JS_ThrowTypeError(
            ctx,
            "%s normalize is gray8/mono1-only and threshold/dither are mono1-only",
            api_name);
        return false;
    }
    out->threshold = (uint8_t)threshold;
    natural_width = rotation == 90 || rotation == 270
                        ? out->source_height
                        : out->source_width;
    natural_height = rotation == 90 || rotation == 270
                         ? out->source_width
                         : out->source_height;
    if (convert) {
        uint32_t width = natural_width;
        uint32_t height = natural_height;
        bool width_present;
        bool height_present;

        if (!read_u32_property(ctx, options, "width", api_name, &width,
                               &width_present) ||
            !read_u32_property(ctx, options, "height", api_name, &height,
                               &height_present) ||
            width == 0 || height == 0 || width > BITMAP_MAX_DIMENSION ||
            height > BITMAP_MAX_DIMENSION) {
            if (!JS_HasException(ctx)) {
                JS_ThrowRangeError(ctx, "%s output dimensions are invalid",
                                   api_name);
            }
            return false;
        }
        (void)width_present;
        (void)height_present;
        out->destination_x = 0;
        out->destination_y = 0;
        out->destination_width = width;
        out->destination_height = height;
        *out_convert_width = width;
        *out_convert_height = height;
        return true;
    }
    return parse_destination_rect(ctx, options, api_name, natural_width,
                                  natural_height, out);
}

static bool ranges_overlap(const uint8_t *first,
                           size_t first_length,
                           const uint8_t *second,
                           size_t second_length)
{
    uintptr_t first_start = (uintptr_t)first;
    uintptr_t second_start = (uintptr_t)second;
    uintptr_t first_end = first_length > UINTPTR_MAX - first_start
                              ? UINTPTR_MAX
                              : first_start + first_length;
    uintptr_t second_end = second_length > UINTPTR_MAX - second_start
                               ? UINTPTR_MAX
                               : second_start + second_length;

    return first_start < second_end && second_start < first_end;
}

static void release_state_resources(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->target_bitmap != NULL && state->target_bitmap->write_lease) {
        bitmap_release_write(state->target_bitmap);
    }
    if (state->result_bitmap != NULL && state->result_bitmap->write_lease) {
        bitmap_release_write(state->result_bitmap);
    }
    if (state->source_owner == BITMAP_SOURCE_BITMAP &&
        state->source_bitmap != NULL) {
        bitmap_release_read(state->source_bitmap);
    } else if (state->source_owner == BITMAP_SOURCE_BYTE_VIEW &&
               state->pixels_rooted) {
        esp32_mquickjs_byte_view_release_read(state->ctx,
                                              state->pixels_ref.val);
#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
    } else if (state->source_owner == BITMAP_SOURCE_CAMERA) {
        esp32_mquickjs_camera_frame_release_bitmap_view(&state->camera_lease);
#endif
    }
    state->source_owner = BITMAP_SOURCE_NONE;
    state->source_bitmap = NULL;
    state->target_bitmap = NULL;
    heap_caps_free(state->staging);
    state->staging = NULL;
    if (state->source_rooted) {
        JS_DeleteGCRef(state->ctx, &state->source_ref);
        state->source_rooted = false;
    }
    if (state->pixels_rooted) {
        JS_DeleteGCRef(state->ctx, &state->pixels_ref);
        state->pixels_rooted = false;
    }
    if (state->target_rooted) {
        JS_DeleteGCRef(state->ctx, &state->target_ref);
        state->target_rooted = false;
    }
}

static void destroy_unstarted_state(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    release_state_resources(state);
    bitmap_free(state->result_bitmap);
    state->result_bitmap = NULL;
    heap_caps_free(state);
}

static bool prepare_convert(
    JSContext *ctx,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    JSCStringBuf format_buf;
    JSCStringBuf layout_buf;
    JSCStringBuf storage_buf;
    const char *format = NULL;
    const char *layout = NULL;
    const char *storage = NULL;
    bool format_present;
    bool layout_present;
    bool storage_present;
    bool stride_present;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint8_t storage_value;
    esp32_mquickjs_bitmap_pixel_format_t target_format;
    esp32_mquickjs_bitmap_layout_t target_layout;
    uint32_t foreground;

    if (out_state == NULL || argc != 2 || !value_is_object(ctx, argv[1].val)) {
        JS_ThrowTypeError(ctx, "%s expects source and options", BITMAP_CONVERT_API);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->completed, false);
    atomic_init(&state->cancelled, false);
    state->ctx = ctx;
    state->convert = true;
    if (!source_from_value(ctx, argv[0].val, BITMAP_CONVERT_API, state) ||
        !read_string_property(ctx, argv[1].val, "format", BITMAP_CONVERT_API,
                              &format, &format_buf, &format_present) ||
        !format_present || !parse_format(format, &target_format) ||
        !read_string_property(ctx, argv[1].val, "layout", BITMAP_CONVERT_API,
                              &layout, &layout_buf, &layout_present) ||
        !parse_layout(layout_present ? layout : NULL, target_format, false,
                      &target_layout) ||
        !read_string_property(ctx, argv[1].val, "storage", BITMAP_CONVERT_API,
                              &storage, &storage_buf, &storage_present) ||
        !parse_storage(storage_present ? storage : NULL, &storage_value) ||
        !read_u32_property(ctx, argv[1].val, "stride", BITMAP_CONVERT_API,
                           &stride, &stride_present) ||
        !parse_transform_options(ctx, argv[1].val, BITMAP_CONVERT_API,
                                 &state->source, target_format, true,
                                 &state->options, &width, &height)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx, "%s received invalid output options",
                              BITMAP_CONVERT_API);
        }
        destroy_unstarted_state(state);
        return false;
    }
    foreground = target_format == ESP32_MQUICKJS_BITMAP_FORMAT_MONO1
                     ? 1U
                     : (target_format == ESP32_MQUICKJS_BITMAP_FORMAT_GRAY8
                            ? 0xffU
                            : (target_format == ESP32_MQUICKJS_BITMAP_FORMAT_RGB565
                                   ? 0xffffU
                                   : 0xffffffU));
    state->result_bitmap = bitmap_allocate(
        ctx, width, height, (uint8_t)target_format, (uint8_t)target_layout,
        storage_value, stride_present ? stride : 0, BITMAP_DEFAULT_CHUNK_BYTES,
        foreground, 0);
    if (state->result_bitmap == NULL ||
        !bitmap_acquire_write(ctx, state->result_bitmap, BITMAP_CONVERT_API)) {
        destroy_unstarted_state(state);
        return false;
    }
    state->target.data = state->result_bitmap->data;
    state->target.length = state->result_bitmap->byte_length;
    state->target.width = state->result_bitmap->width;
    state->target.height = state->result_bitmap->height;
    state->target.stride = state->result_bitmap->stride;
    state->target.format = target_format;
    state->target.layout = target_layout;
    state->target.byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE;
    state->target.bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB;
    *out_state = state;
    return true;
}

static bool prepare_blit(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_bitmap_t *target;
    JSValue options = argc >= 2 ? argv[1].val : JS_UNDEFINED;
    uint32_t ignored_width = 0;
    uint32_t ignored_height = 0;

    if (out_state == NULL || argc < 1 || argc > 2 ||
        (!JS_IsUndefined(options) && !value_is_object(ctx, options))) {
        JS_ThrowTypeError(ctx, "%s expects source and optional options",
                          BITMAP_BLIT_API);
        return false;
    }
    target = bitmap_from_value(ctx, this_ref->val, BITMAP_BLIT_API);
    if (target == NULL) {
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    atomic_init(&state->completed, false);
    atomic_init(&state->cancelled, false);
    state->ctx = ctx;
    state->target_bitmap = target;
    if (!source_from_value(ctx, argv[0].val, BITMAP_BLIT_API, state) ||
        state->source_bitmap == target ||
        ranges_overlap(state->source.data, state->source.length, target->data,
                       target->byte_length) ||
        !root_value(ctx, &state->target_ref, &state->target_rooted,
                    this_ref->val) ||
        !bitmap_acquire_write(ctx, target, BITMAP_BLIT_API)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(ctx,
                              "%s does not allow aliased or in-place input",
                              BITMAP_BLIT_API);
        }
        destroy_unstarted_state(state);
        return false;
    }
    state->target.data = target->data;
    state->target.length = target->byte_length;
    state->target.width = target->width;
    state->target.height = target->height;
    state->target.stride = target->stride;
    state->target.format =
        (esp32_mquickjs_bitmap_pixel_format_t)target->format;
    state->target.layout = (esp32_mquickjs_bitmap_layout_t)target->layout;
    state->target.byte_order = ESP32_MQUICKJS_BITMAP_BYTE_ORDER_BE;
    state->target.bit_order = ESP32_MQUICKJS_BITMAP_BIT_ORDER_LSB;
    if (JS_IsUndefined(options)) {
        JSGCRef empty_ref;
        JSValue *empty = JS_PushGCRef(ctx, &empty_ref);

        *empty = JS_NewObject(ctx);
        if (JS_IsException(*empty) ||
            !parse_transform_options(ctx, *empty, BITMAP_BLIT_API,
                                     &state->source, state->target.format,
                                     false, &state->options, &ignored_width,
                                     &ignored_height)) {
            JS_PopGCRef(ctx, &empty_ref);
            destroy_unstarted_state(state);
            return false;
        }
        JS_PopGCRef(ctx, &empty_ref);
    } else if (!parse_transform_options(
                   ctx, options, BITMAP_BLIT_API, &state->source,
                   state->target.format, false, &state->options,
                   &ignored_width, &ignored_height)) {
        destroy_unstarted_state(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool bitmap_convert_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    (void)this_ref;
    return prepare_convert(ctx, argc, argv, out_state);
}

static bool bitmap_blit_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return prepare_blit(ctx, this_ref, argc, argv, out_state);
}

static bool transform_cancelled(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    return state == NULL || atomic_load_explicit(
                                &state->cancelled, memory_order_acquire);
}

static void bitmap_transform_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state == NULL) {
        return;
    }
    state->result = esp32_mquickjs_bitmap_transform(
        &state->source, &state->target, &state->options,
        transform_cancelled, state, &state->rows_completed, &state->dirty);
    atomic_store_explicit(&state->completed, true, memory_order_release);
}

static bool bitmap_transform_start(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        JS_ThrowInternalError(ctx, "Bitmap transform state is missing");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (!esp32_mquickjs_future_submit_worker(
            runtime, token, bitmap_transform_worker, state)) {
        JS_ThrowInternalError(ctx, "Bitmap transform worker queue is full");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t bitmap_transform_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue bitmap_transform_finish(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    JSValue result;

    if (state == NULL || state->result == ESP32_MQUICKJS_BITMAP_TRANSFORM_INVALID) {
        return JS_ThrowInternalError(ctx, "Bitmap transform failed");
    }
    if (!state->convert && state->target_bitmap != NULL &&
        (state->result == ESP32_MQUICKJS_BITMAP_TRANSFORM_OK ||
         state->rows_completed != 0)) {
        mark_dirty(state->target_bitmap, state->options.destination_x,
                   state->options.destination_y,
                   (int)state->options.destination_width,
                   (int)state->options.destination_height);
    }
    if (state->result == ESP32_MQUICKJS_BITMAP_TRANSFORM_CANCELLED ||
        atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        return JS_ThrowInternalError(ctx, "Bitmap transform cancelled");
    }
    if (!state->convert) {
        return state->target_ref.val;
    }
    bitmap_release_write(state->result_bitmap);
    mark_dirty(state->result_bitmap, 0, 0, state->result_bitmap->width,
               state->result_bitmap->height);
    result = bitmap_wrap(ctx, state->result_bitmap);
    if (!JS_IsException(result)) {
        state->result_bitmap = NULL;
    }
    return result;
}

static bool bitmap_transform_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire) ||
        atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        return false;
    }
    atomic_store_explicit(&state->cancelled, true, memory_order_release);
    return true;
}

static void bitmap_transform_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    release_state_resources(state);
    bitmap_free(state->result_bitmap);
    state->result_bitmap = NULL;
    heap_caps_free(state);
}

static const esp32_mquickjs_future_driver_t s_bitmap_convert_driver = {
    .prepare = bitmap_convert_prepare,
    .start = bitmap_transform_start,
    .poll = bitmap_transform_poll,
    .finish = bitmap_transform_finish,
    .cancel = bitmap_transform_cancel,
    .destroy = bitmap_transform_destroy,
};

static const esp32_mquickjs_future_driver_t s_bitmap_blit_driver = {
    .prepare = bitmap_blit_prepare,
    .start = bitmap_transform_start,
    .poll = bitmap_transform_poll,
    .finish = bitmap_transform_finish,
    .cancel = bitmap_transform_cancel,
    .destroy = bitmap_transform_destroy,
};

bool esp32_mquickjs_init_bitmap_runtime(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref;
    JSGCRef module_ref;
    JSGCRef convert_ref;
    JSGCRef object_ref;
    JSGCRef blit_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    JSValue *convert = JS_PushGCRef(ctx, &convert_ref);
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *blit = JS_PushGCRef(ctx, &blit_ref);
    bool result;

    *global = JS_GetGlobalObject(ctx);
    *module = JS_IsException(*global)
                  ? JS_EXCEPTION
                  : JS_GetPropertyStr(ctx, *global, "bitmap");
    *convert = JS_IsException(*module)
                   ? JS_EXCEPTION
                   : JS_GetPropertyStr(ctx, *module, "convert");
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BITMAP);
    *blit = JS_IsException(*object)
                ? JS_EXCEPTION
                : JS_GetPropertyStr(ctx, *object, "blit");
    result = !JS_IsException(*convert) && !JS_IsException(*blit) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *convert, &s_bitmap_convert_driver) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *blit, &s_bitmap_blit_driver);
    if (!result && !JS_HasException(ctx)) {
        JS_ThrowInternalError(ctx, "failed to register Bitmap Future drivers");
    }
    JS_PopGCRef(ctx, &blit_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &convert_ref);
    JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

void esp32_mquickjs_deinit_bitmap_runtime(JSContext *ctx)
{
    (void)ctx;
}

JSValue js_bitmap_convert(JSContext *ctx, JSValue *this_val,
                          int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, "convert");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

JSValue js_bitmap_blit(JSContext *ctx, JSValue *this_val,
                       int argc, JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, "blit");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

#endif
