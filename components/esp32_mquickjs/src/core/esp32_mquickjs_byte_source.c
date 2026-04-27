#include "utils/esp32_mquickjs_byte_source.h"

#include <limits.h>
#include <string.h>

#include "esp_heap_caps.h"

#define ESP32_MQUICKJS_BYTE_VIEW_OWNER_KEY "__esp32qjsByteViewOwner"
#define ESP32_MQUICKJS_BYTE_SPAN_SOURCE_OWNER_KEY "__esp32qjsByteSpanSourceOwner"
#define ESP32_MQUICKJS_BYTE_VIEW_GC_PRESSURE_THRESHOLD 1024U

typedef struct {
    const uint8_t *data;
    size_t length;
    uint8_t *owned_data;
} esp32_mquickjs_byte_view_t;

typedef struct {
    const esp32_mquickjs_byte_span_source_object_ops_t *ops;
    void *opaque;
    bool closed;
} esp32_mquickjs_byte_span_source_object_t;

static size_t s_byte_view_gc_pressure;

static void note_byte_view_gc_pressure(size_t bytes)
{
    if (bytes == 0) {
        return;
    }
    if (s_byte_view_gc_pressure > SIZE_MAX - bytes) {
        s_byte_view_gc_pressure = ESP32_MQUICKJS_BYTE_VIEW_GC_PRESSURE_THRESHOLD;
        return;
    }
    s_byte_view_gc_pressure += bytes;
}

bool esp32_mquickjs_byte_source_take_gc_request(void)
{
    if (s_byte_view_gc_pressure < ESP32_MQUICKJS_BYTE_VIEW_GC_PRESSURE_THRESHOLD) {
        return false;
    }
    s_byte_view_gc_pressure = 0;
    return true;
}

static bool js_value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < 0) {
        return false;
    }
    *out_value = (uint32_t)raw_value;
    return true;
}

static bool js_value_to_i32(JSContext *ctx, JSValue value, int32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return false;
    }
    *out_value = (int32_t)raw_value;
    return true;
}

static esp32_mquickjs_byte_view_t *byte_view_from_value(JSContext *ctx,
                                                        JSValue value,
                                                        const char *api_name)
{
    esp32_mquickjs_byte_view_t *view;

    if (JS_GetClassID(ctx, value) != JS_CLASS_BYTE_VIEW) {
        JS_ThrowTypeError(ctx, "%s expects a ByteView", api_name);
        return NULL;
    }
    view = JS_GetOpaque(ctx, value);
    if (view == NULL || view->data == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the ByteView is closed", api_name);
        return NULL;
    }
    return view;
}

static esp32_mquickjs_byte_span_source_object_t *byte_span_source_from_value(JSContext *ctx,
                                                                            JSValue value,
                                                                            const char *api_name)
{
    esp32_mquickjs_byte_span_source_object_t *source;

    if (JS_GetClassID(ctx, value) != JS_CLASS_BYTE_SPAN_SOURCE) {
        JS_ThrowTypeError(ctx, "%s expects a ByteSpanSource", api_name);
        return NULL;
    }
    source = JS_GetOpaque(ctx, value);
    if (source == NULL || source->closed) {
        JS_ThrowReferenceError(ctx, "%s failed because the ByteSpanSource is closed", api_name);
        return NULL;
    }
    return source;
}

static JSValue byte_view_make(JSContext *ctx,
                              JSValue owner,
                              const uint8_t *data,
                              uint8_t *owned_data,
                              size_t length)
{
    JSGCRef object_ref;
    JSValue *object;
    esp32_mquickjs_byte_view_t *view;

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BYTE_VIEW);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &object_ref);
        heap_caps_free(owned_data);
        return JS_EXCEPTION;
    }

    view = heap_caps_malloc(sizeof(*view), MALLOC_CAP_8BIT);
    if (view == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        heap_caps_free(owned_data);
        return JS_ThrowOutOfMemory(ctx);
    }
    view->data = data;
    view->length = length;
    view->owned_data = owned_data;
    JS_SetOpaque(ctx, *object, view);

    if (!JS_IsUndefined(owner) &&
        JS_IsException(JS_SetPropertyStr(ctx, *object, ESP32_MQUICKJS_BYTE_VIEW_OWNER_KEY, owner))) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(view->owned_data);
        heap_caps_free(view);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }

    note_byte_view_gc_pressure(sizeof(*view) + (owned_data != NULL ? length : 0U));
    return JS_PopGCRef(ctx, &object_ref);
}

static bool js_value_to_array_bytes(JSContext *ctx,
                                    JSValue value,
                                    const char *api_name,
                                    esp32_mquickjs_byte_source_t *out,
                                    uint8_t **out_owned,
                                    JSValue *out_error)
{
    JSGCRef length_ref;
    JSValue *length_value;
    uint32_t length = 0;
    uint8_t *bytes;
    uint32_t i;

    if (JS_GetClassID(ctx, value) < 0) {
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object of byte values", api_name);
        return false;
    }

    length_value = JS_PushGCRef(ctx, &length_ref);
    *length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(*length_value) || !js_value_to_u32(ctx, *length_value, &length)) {
        JS_PopGCRef(ctx, &length_ref);
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object with a numeric length", api_name);
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);

    if (length == 0) {
        out->data = NULL;
        out->length = 0;
        out->owner = value;
        *out_owned = NULL;
        return true;
    }

    bytes = heap_caps_malloc(length, MALLOC_CAP_8BIT);
    if (bytes == NULL) {
        *out_error = JS_ThrowOutOfMemory(ctx);
        return false;
    }

    for (i = 0; i < length; ++i) {
        JSGCRef item_ref;
        JSValue *item = JS_PushGCRef(ctx, &item_ref);
        uint32_t raw_byte = 0;

        *item = JS_GetPropertyUint32(ctx, value, i);
        if (JS_IsException(*item) || !js_value_to_u32(ctx, *item, &raw_byte) || raw_byte > 0xffU) {
            JS_PopGCRef(ctx, &item_ref);
            heap_caps_free(bytes);
            *out_error = JS_ThrowTypeError(ctx, "%s expects byte values in the range 0-255", api_name);
            return false;
        }
        bytes[i] = (uint8_t)raw_byte;
        JS_PopGCRef(ctx, &item_ref);
    }

    out->data = bytes;
    out->length = length;
    out->owner = value;
    *out_owned = bytes;
    return true;
}

bool esp32_mquickjs_get_byte_source(JSContext *ctx,
                                    JSValue value,
                                    const char *api_name,
                                    esp32_mquickjs_byte_source_t *out,
                                    uint8_t **out_owned,
                                    JSValue *out_error)
{
    esp32_mquickjs_byte_view_t *view;

    if (out == NULL || out_owned == NULL || out_error == NULL) {
        return false;
    }
    out->data = NULL;
    out->length = 0;
    out->owner = JS_UNDEFINED;
    *out_owned = NULL;
    *out_error = JS_UNDEFINED;

    if (JS_GetClassID(ctx, value) == JS_CLASS_BYTE_VIEW) {
        view = byte_view_from_value(ctx, value, api_name);
        if (view == NULL) {
            *out_error = JS_EXCEPTION;
            return false;
        }
        out->data = view->data;
        out->length = view->length;
        out->owner = value;
        return true;
    }

    return js_value_to_array_bytes(ctx, value, api_name, out, out_owned, out_error);
}

bool esp32_mquickjs_get_byte_source_array_length(JSContext *ctx,
                                                 JSValue value,
                                                 const char *api_name,
                                                 uint32_t *out_length,
                                                 JSValue *out_error)
{
    JSGCRef length_ref;
    JSValue *length_value;

    if (out_length == NULL || out_error == NULL) {
        return false;
    }
    *out_length = 0;
    *out_error = JS_UNDEFINED;

    if (JS_GetClassID(ctx, value) < 0) {
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object", api_name);
        return false;
    }

    length_value = JS_PushGCRef(ctx, &length_ref);
    *length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(*length_value) || !js_value_to_u32(ctx, *length_value, out_length)) {
        JS_PopGCRef(ctx, &length_ref);
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object with a numeric length", api_name);
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);
    return true;
}

bool esp32_mquickjs_get_byte_source_chunk(JSContext *ctx,
                                          JSValue chunks,
                                          uint32_t index,
                                          const char *api_name,
                                          esp32_mquickjs_byte_source_chunk_t *out,
                                          JSValue *out_error)
{
    JSValue *value;

    if (out == NULL || out_error == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->source.owner = JS_UNDEFINED;
    *out_error = JS_UNDEFINED;

    value = JS_AddGCRef(ctx, &out->value_ref);
    out->rooted = true;
    *value = JS_GetPropertyUint32(ctx, chunks, index);
    if (JS_IsException(*value)) {
        esp32_mquickjs_release_byte_source_chunk(ctx, out);
        *out_error = JS_EXCEPTION;
        return false;
    }

    if (!esp32_mquickjs_get_byte_source(ctx, *value, api_name, &out->source, &out->owned, out_error)) {
        esp32_mquickjs_release_byte_source_chunk(ctx, out);
        return false;
    }
    return true;
}

void esp32_mquickjs_release_byte_source_chunk(JSContext *ctx,
                                              esp32_mquickjs_byte_source_chunk_t *chunk)
{
    if (chunk == NULL) {
        return;
    }
    esp32_mquickjs_release_byte_source(chunk->owned);
    chunk->owned = NULL;
    if (chunk->rooted) {
        JS_DeleteGCRef(ctx, &chunk->value_ref);
        chunk->rooted = false;
    }
    chunk->source.data = NULL;
    chunk->source.length = 0;
    chunk->source.owner = JS_UNDEFINED;
}

bool esp32_mquickjs_open_byte_span_source(JSContext *ctx,
                                          JSValue value,
                                          const char *api_name,
                                          esp32_mquickjs_byte_span_source_t *out,
                                          JSValue *out_error)
{
    esp32_mquickjs_byte_span_source_object_t *source;

    if (out == NULL || out_error == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    *out_error = JS_UNDEFINED;

    source = byte_span_source_from_value(ctx, value, api_name);
    if (source == NULL) {
        *out_error = JS_EXCEPTION;
        return false;
    }
    if (source->ops == NULL || source->ops->open == NULL) {
        *out_error = JS_ThrowTypeError(ctx, "%s received a ByteSpanSource without an opener", api_name);
        return false;
    }
    return source->ops->open(ctx, value, source->opaque, out, out_error);
}

JSValue esp32_mquickjs_new_byte_span_source(JSContext *ctx,
                                            JSValue owner,
                                            const esp32_mquickjs_byte_span_source_object_ops_t *ops,
                                            void *opaque)
{
    JSGCRef object_ref;
    JSValue *object;
    esp32_mquickjs_byte_span_source_object_t *source;

    if (ops == NULL || ops->open == NULL) {
        if (ops != NULL && ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        return JS_ThrowInternalError(ctx, "ByteSpanSource requires an open callback");
    }

    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BYTE_SPAN_SOURCE);
    if (JS_IsException(*object)) {
        if (ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }

    source = heap_caps_malloc(sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        if (ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    source->ops = ops;
    source->opaque = opaque;
    source->closed = false;
    JS_SetOpaque(ctx, *object, source);

    if (!JS_IsUndefined(owner) &&
        JS_IsException(JS_SetPropertyStr(ctx, *object, ESP32_MQUICKJS_BYTE_SPAN_SOURCE_OWNER_KEY, owner))) {
        JS_SetOpaque(ctx, *object, NULL);
        if (ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        heap_caps_free(source);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &object_ref);
}

JSValue esp32_mquickjs_new_byte_view(JSContext *ctx,
                                     JSValue owner,
                                     const uint8_t *data,
                                     size_t length)
{
    if (data == NULL && length > 0) {
        return JS_ThrowInternalError(ctx, "ByteView data pointer is null");
    }
    return byte_view_make(ctx, owner, data, NULL, length);
}

JSValue esp32_mquickjs_new_owned_byte_view(JSContext *ctx,
                                           uint8_t *data,
                                           size_t length)
{
    if (data == NULL && length > 0) {
        return JS_ThrowInternalError(ctx, "ByteView data pointer is null");
    }
    return byte_view_make(ctx, JS_UNDEFINED, data, data, length);
}

bool esp32_mquickjs_update_byte_view(JSContext *ctx,
                                     JSValue value,
                                     const uint8_t *data,
                                     size_t length)
{
    esp32_mquickjs_byte_view_t *view;

    if (data == NULL && length > 0) {
        JS_ThrowInternalError(ctx, "ByteView data pointer is null");
        return false;
    }

    view = byte_view_from_value(ctx, value, "ByteView update");
    if (view == NULL) {
        return false;
    }
    view->data = data;
    view->length = length;
    return true;
}

void esp32_mquickjs_release_byte_source(uint8_t *owned)
{
    heap_caps_free(owned);
}

JSValue js_byte_view_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "ByteView cannot be constructed directly");
}

JSValue js_byte_span_source_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "ByteSpanSource cannot be constructed directly");
}

void js_byte_view_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_byte_view_t *view = opaque;

    (void)ctx;

    if (view == NULL) {
        return;
    }
    heap_caps_free(view->owned_data);
    heap_caps_free(view);
}

void js_byte_span_source_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_byte_span_source_object_t *source = opaque;

    if (source == NULL) {
        return;
    }
    source->closed = true;
    if (source->ops != NULL && source->ops->destroy != NULL) {
        source->ops->destroy(ctx, source->opaque);
    }
    heap_caps_free(source);
}

JSValue js_byte_view_get_length(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_byte_view_t *view;

    (void)argc;
    (void)argv;

    view = byte_view_from_value(ctx, *this_val, "ByteView.length");
    if (view == NULL) {
        return JS_EXCEPTION;
    }
    return JS_NewUint32(ctx, (uint32_t)view->length);
}

JSValue js_byte_view_to_array(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_byte_view_t *view;
    JSGCRef array_ref;
    JSValue *array_obj;
    size_t i;

    (void)argc;
    (void)argv;

    view = byte_view_from_value(ctx, *this_val, "ByteView.toArray()");
    if (view == NULL) {
        return JS_EXCEPTION;
    }

    array_obj = JS_PushGCRef(ctx, &array_ref);
    *array_obj = JS_NewArray(ctx, 0);
    if (JS_IsException(*array_obj)) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }

    for (i = 0; i < view->length; ++i) {
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array_obj, (uint32_t)i, JS_NewInt32(ctx, view->data[i])))) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
    }

    return JS_PopGCRef(ctx, &array_ref);
}

JSValue js_byte_span_source_set_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_byte_span_source_object_t *source;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    source = byte_span_source_from_value(ctx, *this_val, "ByteSpanSource.setRect()");
    if (source == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 4 ||
        !js_value_to_i32(ctx, argv[0], &x) ||
        !js_value_to_i32(ctx, argv[1], &y) ||
        !js_value_to_i32(ctx, argv[2], &width) ||
        !js_value_to_i32(ctx, argv[3], &height)) {
        return JS_ThrowTypeError(ctx, "ByteSpanSource.setRect(x, y, width, height) expects integers");
    }
    if (source->ops == NULL || source->ops->set_rect == NULL) {
        return JS_ThrowTypeError(ctx, "ByteSpanSource.setRect() is not supported by this source");
    }
    if (!source->ops->set_rect(ctx, source->opaque, x, y, width, height)) {
        return JS_EXCEPTION;
    }
    return *this_val;
}
