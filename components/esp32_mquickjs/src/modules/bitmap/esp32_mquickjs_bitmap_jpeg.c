#include "esp32_mquickjs_bitmap_internal.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP_JPEG

#include "esp32_mquickjs_bitmap_jpeg.h"
#include "esp32_mquickjs_future.h"
#include "utils/esp32_mquickjs_byte_source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"

#define BITMAP_DECODE_API "Bitmap.decode()"
#define BITMAP_JPEG_MAX_CHUNKS 32U

struct esp32_mquickjs_future_driver_state {
    JSContext *ctx;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    JSGCRef target_ref;
    bool target_rooted;
    bool target_write_acquired;
    bool started;
    bool published;
    _Atomic bool completed;
    _Atomic bool cancelled;
    _Atomic bool publishing;
    esp32_mquickjs_bitmap_t *target;
    uint8_t *input;
    size_t input_length;
    uint8_t *decoded;
    size_t decoded_length;
    uint16_t width;
    uint16_t height;
    int32_t destination_x;
    int32_t destination_y;
    esp_err_t error;
};

static bool jpeg_value_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0 && !JS_IsArray(ctx, value);
}

static bool jpeg_root_value(JSContext *ctx,
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

static bool jpeg_read_u32(JSContext *ctx,
                          JSValue object,
                          const char *name,
                          uint32_t *out,
                          bool *present)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool result = true;

    *present = false;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) {
        result = false;
    } else if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        if (!value_to_u32(ctx, *property, out)) {
            JS_ThrowTypeError(ctx, "%s option '%s' expects a non-negative integer",
                              BITMAP_DECODE_API, name);
            result = false;
        } else {
            *present = true;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool jpeg_read_i32(JSContext *ctx,
                          JSValue object,
                          const char *name,
                          int32_t *out,
                          bool *present)
{
    JSGCRef property_ref;
    JSValue *property = JS_PushGCRef(ctx, &property_ref);
    bool result = true;

    *present = false;
    *property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(*property)) {
        result = false;
    } else if (!JS_IsUndefined(*property) && !JS_IsNull(*property)) {
        if (!value_to_i32(ctx, *property, out)) {
            JS_ThrowTypeError(ctx, "%s option '%s' expects an integer",
                              BITMAP_DECODE_API, name);
            result = false;
        } else {
            *present = true;
        }
    }
    JS_PopGCRef(ctx, &property_ref);
    return result;
}

static bool jpeg_copy_direct_source(
    JSContext *ctx,
    JSValue value,
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;

    if (!esp32_mquickjs_get_byte_source(
            ctx, value, BITMAP_DECODE_API, &source, &owned, &error)) {
        return false;
    }
    if (source.length == 0U ||
        source.length > CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_INPUT_BYTES) {
        esp32_mquickjs_release_byte_source(owned);
        JS_ThrowRangeError(ctx, "%s input must contain 1..%u bytes",
                           BITMAP_DECODE_API,
                           CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_INPUT_BYTES);
        return false;
    }
    if (owned != NULL) {
        state->input = owned;
    } else {
        state->input = esp32_mquickjs_memory_payload_alloc(
            "bitmap.jpeg.input", source.length,
            ESP32_MQUICKJS_MEMORY_DEFAULT);
        if (state->input == NULL) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        memcpy(state->input, source.data, source.length);
    }
    state->input_length = source.length;
    return true;
}

static bool jpeg_copy_segmented_source(
    JSContext *ctx,
    JSValue descriptor,
    JSValue chunks,
    esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef lengths_ref;
    JSValue *lengths = JS_PushGCRef(ctx, &lengths_ref);
    JSValue error = JS_UNDEFINED;
    uint32_t chunk_count = 0;
    uint32_t lengths_count = 0;
    uint32_t byte_length = 0;
    bool byte_length_present = false;
    uint32_t index;
    size_t copied = 0;
    bool result = false;

    *lengths = JS_GetPropertyStr(ctx, descriptor, "lengths");
    if (JS_IsException(*lengths) ||
        !jpeg_read_u32(ctx, descriptor, "byteLength", &byte_length,
                       &byte_length_present) ||
        !byte_length_present || byte_length == 0U ||
        byte_length > CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_INPUT_BYTES ||
        !esp32_mquickjs_get_byte_source_array_length(
            ctx, chunks, BITMAP_DECODE_API " chunks", &chunk_count, &error) ||
        !esp32_mquickjs_get_byte_source_array_length(
            ctx, *lengths, BITMAP_DECODE_API " lengths", &lengths_count,
            &error) ||
        chunk_count == 0U || chunk_count > BITMAP_JPEG_MAX_CHUNKS ||
        chunk_count != lengths_count) {
        if (!JS_HasException(ctx)) {
            JS_ThrowRangeError(
                ctx,
                "%s segmented source requires 1..%u matching chunks/lengths and an exact byteLength no larger than %u",
                BITMAP_DECODE_API, BITMAP_JPEG_MAX_CHUNKS,
                CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_INPUT_BYTES);
        }
        goto done;
    }
    state->input = esp32_mquickjs_memory_payload_alloc(
        "bitmap.jpeg.input", byte_length,
        ESP32_MQUICKJS_MEMORY_DEFAULT);
    if (state->input == NULL) {
        JS_ThrowOutOfMemory(ctx);
        goto done;
    }
    state->input_length = byte_length;
    for (index = 0; index < chunk_count; ++index) {
        JSGCRef useful_ref;
        JSValue *useful_value = JS_PushGCRef(ctx, &useful_ref);
        esp32_mquickjs_byte_source_chunk_t chunk;
        uint32_t useful = 0;

        *useful_value = JS_GetPropertyUint32(ctx, *lengths, index);
        if (JS_IsException(*useful_value) ||
            !value_to_u32(ctx, *useful_value, &useful) || useful == 0U ||
            !esp32_mquickjs_get_byte_source_chunk(
                ctx, chunks, index, BITMAP_DECODE_API " chunks", &chunk,
                &error)) {
            JS_PopGCRef(ctx, &useful_ref);
            if (!JS_HasException(ctx)) {
                JS_ThrowTypeError(ctx,
                                  "%s lengths must contain positive integers",
                                  BITMAP_DECODE_API);
            }
            goto done;
        }
        if ((size_t)useful > chunk.source.length || useful > byte_length - copied) {
            esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
            JS_PopGCRef(ctx, &useful_ref);
            JS_ThrowRangeError(ctx,
                               "%s chunk length exceeds its source or byteLength",
                               BITMAP_DECODE_API);
            goto done;
        }
        memcpy(state->input + copied, chunk.source.data, useful);
        copied += useful;
        esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
        JS_PopGCRef(ctx, &useful_ref);
    }
    if (copied != byte_length) {
        JS_ThrowRangeError(ctx,
                           "%s lengths must sum exactly to byteLength",
                           BITMAP_DECODE_API);
        goto done;
    }
    result = true;

done:
    JS_PopGCRef(ctx, &lengths_ref);
    return result;
}

static bool jpeg_copy_source(JSContext *ctx,
                             JSValue source,
                             esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef chunks_ref;
    JSValue *chunks = JS_PushGCRef(ctx, &chunks_ref);
    bool result;

    *chunks = jpeg_value_is_object(ctx, source)
                  ? JS_GetPropertyStr(ctx, source, "chunks")
                  : JS_UNDEFINED;
    if (JS_IsException(*chunks)) {
        result = false;
    } else if (!JS_IsUndefined(*chunks) && !JS_IsNull(*chunks)) {
        result = jpeg_copy_segmented_source(ctx, source, *chunks, state);
    } else {
        result = jpeg_copy_direct_source(ctx, source, state);
    }
    JS_PopGCRef(ctx, &chunks_ref);
    return result;
}

static bool jpeg_parse_codec(JSContext *ctx, JSValue options)
{
    JSGCRef codec_ref;
    JSValue *codec = JS_PushGCRef(ctx, &codec_ref);
    JSCStringBuf codec_buf;
    const char *name = NULL;
    bool result = false;

    *codec = JS_GetPropertyStr(ctx, options, "codec");
    if (!JS_IsException(*codec) && JS_IsString(ctx, *codec) &&
        (name = JS_ToCString(ctx, *codec, &codec_buf)) != NULL &&
        strcmp(name, "jpeg") == 0) {
        result = true;
    } else if (!JS_HasException(ctx)) {
        JS_ThrowTypeError(ctx, "%s codec must be 'jpeg'", BITMAP_DECODE_API);
    }
    JS_PopGCRef(ctx, &codec_ref);
    return result;
}

static bool jpeg_parse_destination(
    JSContext *ctx,
    JSValue options,
    const esp32_mquickjs_bitmap_jpeg_info_t *info,
    esp32_mquickjs_bitmap_t *target,
    esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef rect_ref;
    JSValue *rect = JS_PushGCRef(ctx, &rect_ref);
    uint32_t width = info->width;
    uint32_t height = info->height;
    bool present = false;
    bool result = false;

    state->destination_x = 0;
    state->destination_y = 0;
    *rect = JS_GetPropertyStr(ctx, options, "destinationRect");
    if (JS_IsException(*rect)) {
        goto done;
    }
    if (!JS_IsUndefined(*rect) && !JS_IsNull(*rect)) {
        bool width_present = false;
        bool height_present = false;

        if (!jpeg_value_is_object(ctx, *rect) ||
            !jpeg_read_i32(ctx, *rect, "x", &state->destination_x, &present) ||
            !jpeg_read_i32(ctx, *rect, "y", &state->destination_y, &present) ||
            !jpeg_read_u32(ctx, *rect, "width", &width, &width_present) ||
            !jpeg_read_u32(ctx, *rect, "height", &height, &height_present) ||
            !width_present || !height_present) {
            if (!JS_HasException(ctx)) {
                JS_ThrowTypeError(ctx,
                                  "%s destinationRect requires x, y, width, and height",
                                  BITMAP_DECODE_API);
            }
            goto done;
        }
    }
    if (width != info->width || height != info->height) {
        JS_ThrowRangeError(ctx,
                           "%s does not resize JPEG input; destination dimensions must match the image",
                           BITMAP_DECODE_API);
        goto done;
    }
    if (state->destination_x < 0 || state->destination_y < 0 ||
        (uint32_t)state->destination_x > target->width - width ||
        (uint32_t)state->destination_y > target->height - height) {
        JS_ThrowRangeError(ctx, "%s destination is outside the Bitmap",
                           BITMAP_DECODE_API);
        goto done;
    }
    result = true;

done:
    JS_PopGCRef(ctx, &rect_ref);
    return result;
}

static void bitmap_jpeg_decode_destroy_unstarted(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->target_write_acquired && state->target != NULL) {
        bitmap_release_write(state->target);
    }
    if (state->target_rooted) {
        JS_DeleteGCRef(state->ctx, &state->target_ref);
    }
    esp32_mquickjs_memory_payload_free(state->decoded);
    esp32_mquickjs_memory_payload_free(state->input);
    heap_caps_free(state);
}

static bool bitmap_jpeg_decode_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state = NULL;
    esp32_mquickjs_bitmap_t *target;
    esp32_mquickjs_bitmap_jpeg_info_t info;
    esp32_mquickjs_bitmap_jpeg_result_t parse_result;
    size_t decoded_length;

    if (out_state == NULL || argc != 2 ||
        !jpeg_value_is_object(ctx, argv[1].val)) {
        JS_ThrowTypeError(ctx, "%s expects source and options", BITMAP_DECODE_API);
        return false;
    }
    target = bitmap_from_value(ctx, this_ref->val, BITMAP_DECODE_API);
    if (target == NULL) {
        return false;
    }
    if (target->format != BITMAP_FORMAT_RGB565 ||
        target->layout != BITMAP_LAYOUT_LINEAR) {
        JS_ThrowTypeError(ctx,
                          "%s currently requires a linear rgb565 destination",
                          BITMAP_DECODE_API);
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->ctx = ctx;
    state->target = target;
    state->error = ESP_OK;
    atomic_init(&state->completed, false);
    atomic_init(&state->cancelled, false);
    atomic_init(&state->publishing, false);
    if (!jpeg_parse_codec(ctx, argv[1].val) ||
        !jpeg_copy_source(ctx, argv[0].val, state)) {
        bitmap_jpeg_decode_destroy_unstarted(state);
        return false;
    }
    parse_result = esp32_mquickjs_bitmap_jpeg_parse(
        state->input, state->input_length, &info);
    if (parse_result != ESP32_MQUICKJS_BITMAP_JPEG_OK) {
        if (parse_result == ESP32_MQUICKJS_BITMAP_JPEG_UNSUPPORTED) {
            JS_ThrowTypeError(ctx,
                              "%s supports baseline JPEG input only",
                              BITMAP_DECODE_API);
        } else {
            JS_ThrowTypeError(ctx, "%s received malformed or truncated JPEG",
                              BITMAP_DECODE_API);
        }
        bitmap_jpeg_decode_destroy_unstarted(state);
        return false;
    }
    if ((uint32_t)info.width > target->width ||
        (uint32_t)info.height > target->height ||
        !jpeg_parse_destination(ctx, argv[1].val, &info, target, state)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowRangeError(ctx, "%s image is larger than the destination",
                               BITMAP_DECODE_API);
        }
        bitmap_jpeg_decode_destroy_unstarted(state);
        return false;
    }
    decoded_length = (size_t)info.width * info.height * 2U;
    if (decoded_length > CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_OUTPUT_BYTES) {
        JS_ThrowRangeError(ctx, "%s decoded output exceeds %u bytes",
                           BITMAP_DECODE_API,
                           CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_OUTPUT_BYTES);
        bitmap_jpeg_decode_destroy_unstarted(state);
        return false;
    }
    state->decoded = esp32_mquickjs_memory_payload_alloc(
        "bitmap.jpeg.decoded", decoded_length,
        ESP32_MQUICKJS_MEMORY_DEFAULT);
    if (state->decoded == NULL ||
        !jpeg_root_value(ctx, &state->target_ref, &state->target_rooted,
                         this_ref->val) ||
        !bitmap_acquire_write(ctx, target, BITMAP_DECODE_API)) {
        if (state->decoded == NULL && !JS_HasException(ctx)) {
            JS_ThrowOutOfMemory(ctx);
        }
        bitmap_jpeg_decode_destroy_unstarted(state);
        return false;
    }
    state->target_write_acquired = true;
    state->decoded_length = decoded_length;
    state->width = info.width;
    state->height = info.height;
    *out_state = state;
    return true;
}

static void bitmap_jpeg_decode_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    esp_jpeg_image_cfg_t config;
    esp_jpeg_image_output_t output = {0};
    uint32_t row;

    if (state == NULL) {
        return;
    }
    if (atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        atomic_store_explicit(&state->completed, true, memory_order_release);
        return;
    }
    memset(&config, 0, sizeof(config));
    config.indata = state->input;
    config.indata_size = (uint32_t)state->input_length;
    config.outbuf = state->decoded;
    config.outbuf_size = (uint32_t)state->decoded_length;
    config.out_format = JPEG_IMAGE_FORMAT_RGB565;
    config.out_scale = JPEG_IMAGE_SCALE_0;
    config.flags.swap_color_bytes = 1;
    state->error = esp_jpeg_decode(&config, &output);
    if (state->error == ESP_OK &&
        (output.width != state->width || output.height != state->height ||
         output.output_len != state->decoded_length)) {
        state->error = ESP_FAIL;
    }
    atomic_store_explicit(&state->publishing, true, memory_order_release);
    if (state->error == ESP_OK &&
        !atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        for (row = 0; row < state->height; ++row) {
            memcpy(state->target->data +
                       ((size_t)state->destination_y + row) *
                           state->target->stride +
                       (size_t)state->destination_x * 2U,
                   state->decoded + (size_t)row * state->width * 2U,
                   (size_t)state->width * 2U);
        }
        state->published = true;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
}

static bool bitmap_jpeg_decode_start(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (!esp32_mquickjs_future_submit_worker(
            runtime, token, bitmap_jpeg_decode_worker, state)) {
        JS_ThrowInternalError(ctx, "Bitmap JPEG decoder worker queue is full");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t bitmap_jpeg_decode_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
               ? ESP32_MQUICKJS_FUTURE_READY
               : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue bitmap_jpeg_decode_finish(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);

    if (state == NULL ||
        atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowInternalError(ctx, "Bitmap JPEG decode cancelled");
    }
    if (state->error != ESP_OK || !state->published) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_ThrowInternalError(ctx, "Bitmap JPEG decode failed (%d)",
                                     (int)state->error);
    }
    mark_dirty(state->target, state->destination_x, state->destination_y,
               state->width, state->height);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "codec", JS_NewString(ctx, "jpeg")) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "engine",
            JS_NewString(ctx,
#if CONFIG_JD_USE_ROM
                         "rom-tjpgd"
#else
                         "software-tjpgd"
#endif
                         )) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "width", JS_NewUint32(ctx, state->width)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "height", JS_NewUint32(ctx, state->height)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "inputBytes",
            JS_NewInt64(ctx, (int64_t)state->input_length)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, result, "outputBytes",
            JS_NewInt64(ctx, (int64_t)state->decoded_length))) {
        JS_PopGCRef(ctx, &result_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &result_ref);
}

static esp32_mquickjs_cancel_result_t bitmap_jpeg_decode_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire) ||
        atomic_load_explicit(&state->publishing, memory_order_acquire) ||
        atomic_load_explicit(&state->cancelled, memory_order_acquire)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    atomic_store_explicit(&state->cancelled, true, memory_order_release);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}

static void bitmap_jpeg_decode_destroy(
    esp32_mquickjs_future_driver_state_t *state)
{
    bitmap_jpeg_decode_destroy_unstarted(state);
}

static const esp32_mquickjs_future_driver_t s_bitmap_decode_driver = {
    .capture = bitmap_jpeg_decode_prepare,
    .start = bitmap_jpeg_decode_start,
    .poll = bitmap_jpeg_decode_poll,
    .finish = bitmap_jpeg_decode_finish,
    .cancel = bitmap_jpeg_decode_cancel,
    .destroy = bitmap_jpeg_decode_destroy,
};

bool esp32_mquickjs_register_bitmap_jpeg_driver(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    JSValue decode_function)
{
    return esp32_mquickjs_future_register_driver(
        ctx, runtime, decode_function, &s_bitmap_decode_driver);
}

JSValue js_bitmap_decode(JSContext *ctx,
                         JSValue *this_val,
                         int argc,
                         JSValue *argv)
{
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *method = JS_GetPropertyStr(ctx, *this_val, "decode");
    result = JS_IsException(*method)
                 ? JS_EXCEPTION
                 : esp32_mquickjs_future_call_and_wait(
                       ctx, esp32_mquickjs_get_active_runtime(), *method,
                       *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

#endif
