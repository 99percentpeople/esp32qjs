#include "utils/esp32_mquickjs_byte_source.h"

#include <limits.h>
#include <string.h>

#include "esp_heap_caps.h"

#define ESP32_MQUICKJS_BYTE_VIEW_OWNER_KEY "__esp32qjsByteViewOwner"
#define ESP32_MQUICKJS_BYTE_SPAN_SOURCE_OWNER_KEY "__esp32qjsByteSpanSourceOwner"
typedef struct {
    const uint8_t *data;
    size_t length;
    uint8_t *owned_data;
    uint16_t read_leases;
    bool closed;
} esp32_mquickjs_byte_view_t;

typedef struct {
    const esp32_mquickjs_byte_span_source_object_ops_t *ops;
    void *opaque;
    uint16_t read_leases;
    bool closed;
} esp32_mquickjs_byte_span_source_object_t;

typedef struct {
    esp32_mquickjs_byte_span_source_t inner;
    esp32_mquickjs_byte_span_source_object_t *owner;
    JSGCRef owner_ref;
    bool owner_rooted;
} esp32_mquickjs_leased_byte_span_source_t;

static bool leased_byte_span_source_next(
    JSContext *ctx,
    void *opaque,
    esp32_mquickjs_byte_span_t *span)
{
    esp32_mquickjs_leased_byte_span_source_t *source = opaque;

    return source != NULL &&
           esp32_mquickjs_byte_span_source_next(ctx, &source->inner, span);
}

static void leased_byte_span_source_close(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_leased_byte_span_source_t *source = opaque;

    if (source == NULL) {
        return;
    }
    esp32_mquickjs_byte_span_source_close(ctx, &source->inner);
    if (source->owner != NULL && source->owner->read_leases > 0) {
        source->owner->read_leases--;
    }
    if (source->owner_rooted) {
        JS_DeleteGCRef(ctx, &source->owner_ref);
    }
    heap_caps_free(source);
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
    if (view == NULL || view->closed) {
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
    int class_id = JS_GetClassID(ctx, value);

    if (class_id != JS_CLASS_BYTE_SPAN_SOURCE && class_id != JS_CLASS_BITMAP_SPAN_SOURCE) {
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
    JSGCRef owner_ref;
    JSValue *object;
    JSValue *rooted_owner;
    esp32_mquickjs_byte_view_t *view;

    object = JS_PushGCRef(ctx, &object_ref);
    rooted_owner = JS_PushGCRef(ctx, &owner_ref);
    *object = JS_UNDEFINED;
    *rooted_owner = owner;
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_BYTE_VIEW);
    if (JS_IsException(*object)) {
        JS_PopGCRef(ctx, &owner_ref);
        JS_PopGCRef(ctx, &object_ref);
        heap_caps_free(owned_data);
        return JS_EXCEPTION;
    }

    view = heap_caps_malloc(sizeof(*view), MALLOC_CAP_8BIT);
    if (view == NULL) {
        JS_PopGCRef(ctx, &owner_ref);
        JS_PopGCRef(ctx, &object_ref);
        heap_caps_free(owned_data);
        return JS_ThrowOutOfMemory(ctx);
    }
    view->data = data;
    view->length = length;
    view->owned_data = owned_data;
    view->read_leases = 0;
    view->closed = false;
    JS_SetOpaque(ctx, *object, view);

    if (!JS_IsUndefined(*rooted_owner) &&
        JS_IsException(JS_SetPropertyStr(ctx, *object, ESP32_MQUICKJS_BYTE_VIEW_OWNER_KEY, *rooted_owner))) {
        JS_SetOpaque(ctx, *object, NULL);
        heap_caps_free(view->owned_data);
        heap_caps_free(view);
        JS_PopGCRef(ctx, &owner_ref);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }

    JS_PopGCRef(ctx, &owner_ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static bool js_value_to_array_bytes(JSContext *ctx,
                                    JSValue *value,
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

    if (JS_GetClassID(ctx, *value) < 0) {
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object of byte values", api_name);
        return false;
    }

    length_value = JS_PushGCRef(ctx, &length_ref);
    *length_value = JS_GetPropertyStr(ctx, *value, "length");
    if (JS_IsException(*length_value) || !js_value_to_u32(ctx, *length_value, &length)) {
        JS_PopGCRef(ctx, &length_ref);
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object with a numeric length", api_name);
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);

    if (length == 0) {
        out->data = NULL;
        out->length = 0;
        out->owner = *value;
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

        *item = JS_GetPropertyUint32(ctx, *value, i);
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
    out->owner = *value;
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
    JSGCRef value_ref;
    JSValue *rooted_value;
    esp32_mquickjs_byte_view_t *view;
    bool result;

    if (out == NULL || out_owned == NULL || out_error == NULL) {
        return false;
    }
    out->data = NULL;
    out->length = 0;
    out->owner = JS_UNDEFINED;
    *out_owned = NULL;
    *out_error = JS_UNDEFINED;

    rooted_value = JS_PushGCRef(ctx, &value_ref);
    *rooted_value = value;
    if (JS_GetClassID(ctx, *rooted_value) == JS_CLASS_BYTE_VIEW) {
        view = byte_view_from_value(ctx, *rooted_value, api_name);
        if (view == NULL) {
            *out_error = JS_EXCEPTION;
            result = false;
        } else {
            out->data = view->data;
            out->length = view->length;
            out->owner = *rooted_value;
            result = true;
        }
    } else {
        result = js_value_to_array_bytes(ctx, rooted_value, api_name, out, out_owned, out_error);
    }
    JS_PopGCRef(ctx, &value_ref);
    return result;
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
    if (JS_GetClassID(ctx, *value) == JS_CLASS_BYTE_VIEW) {
        const uint8_t *leased_data = NULL;
        size_t leased_length = 0;

        if (!esp32_mquickjs_byte_view_acquire_read(
                ctx, *value, api_name, &leased_data, &leased_length)) {
            *out_error = JS_EXCEPTION;
            esp32_mquickjs_release_byte_source_chunk(ctx, out);
            return false;
        }
        out->source.data = leased_data;
        out->source.length = leased_length;
        out->read_leased = true;
    }
    return true;
}

void esp32_mquickjs_release_byte_source_chunk(JSContext *ctx,
                                              esp32_mquickjs_byte_source_chunk_t *chunk)
{
    if (chunk == NULL) {
        return;
    }
    if (chunk->read_leased && chunk->rooted) {
        esp32_mquickjs_byte_view_release_read(ctx, chunk->value_ref.val);
        chunk->read_leased = false;
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
    esp32_mquickjs_leased_byte_span_source_t *lease;

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
    if (source->read_leases == UINT16_MAX) {
        *out_error = JS_ThrowInternalError(
            ctx, "%s could not acquire a ByteSpanSource read lease", api_name);
        return false;
    }
    lease = heap_caps_calloc(1, sizeof(*lease), MALLOC_CAP_8BIT);
    if (lease == NULL) {
        *out_error = JS_ThrowOutOfMemory(ctx);
        return false;
    }
    *JS_AddGCRef(ctx, &lease->owner_ref) = value;
    lease->owner_rooted = true;
    if (!source->ops->open(ctx, value, source->opaque, &lease->inner,
                           out_error)) {
        JS_DeleteGCRef(ctx, &lease->owner_ref);
        heap_caps_free(lease);
        return false;
    }
    if (lease->inner.next == NULL) {
        esp32_mquickjs_byte_span_source_close(ctx, &lease->inner);
        JS_DeleteGCRef(ctx, &lease->owner_ref);
        heap_caps_free(lease);
        *out_error = JS_ThrowInternalError(
            ctx, "%s received a ByteSpanSource without an iterator", api_name);
        return false;
    }
    source->read_leases++;
    lease->owner = source;
    out->opaque = lease;
    out->next = leased_byte_span_source_next;
    out->close = leased_byte_span_source_close;
    return true;
}

bool esp32_mquickjs_byte_span_source_known_length(JSContext *ctx,
                                                  JSValue value,
                                                  size_t *out_length)
{
    esp32_mquickjs_byte_span_source_object_t *source;

    if (out_length == NULL) {
        return false;
    }
    source = byte_span_source_from_value(ctx, value, "ByteSpanSource length");
    if (source == NULL || source->ops == NULL || source->ops->known_length == NULL) {
        return false;
    }
    *out_length = source->ops->known_length(source->opaque);
    return true;
}

void *esp32_mquickjs_byte_span_source_get_opaque(
    JSContext *ctx,
    JSValue value,
    const esp32_mquickjs_byte_span_source_object_ops_t *expected_ops,
    const char *api_name)
{
    esp32_mquickjs_byte_span_source_object_t *source =
        byte_span_source_from_value(ctx, value, api_name);

    if (source == NULL) {
        return NULL;
    }
    if (source->ops != expected_ops) {
        JS_ThrowTypeError(ctx, "%s expects an RPC file ByteSpanSource", api_name);
        return NULL;
    }
    return source->opaque;
}

JSValue esp32_mquickjs_new_byte_span_source(JSContext *ctx,
                                            JSValue owner,
                                            const esp32_mquickjs_byte_span_source_object_ops_t *ops,
                                            void *opaque)
{
    JSGCRef object_ref;
    JSGCRef owner_ref;
    JSValue *object;
    JSValue *rooted_owner;
    esp32_mquickjs_byte_span_source_object_t *source;
    int class_id;

    if (ops == NULL || ops->open == NULL) {
        if (ops != NULL && ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        return JS_ThrowInternalError(ctx, "ByteSpanSource requires an open callback");
    }
    class_id = ops->class_id != 0 ? ops->class_id : JS_CLASS_BYTE_SPAN_SOURCE;
    if (class_id != JS_CLASS_BYTE_SPAN_SOURCE && class_id != JS_CLASS_BITMAP_SPAN_SOURCE) {
        if (ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        return JS_ThrowInternalError(ctx, "ByteSpanSource received unsupported source class");
    }

    object = JS_PushGCRef(ctx, &object_ref);
    rooted_owner = JS_PushGCRef(ctx, &owner_ref);
    *object = JS_UNDEFINED;
    *rooted_owner = owner;
    *object = JS_NewObjectClassUser(ctx, class_id);
    if (JS_IsException(*object)) {
        if (ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        JS_PopGCRef(ctx, &owner_ref);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }

    source = heap_caps_malloc(sizeof(*source), MALLOC_CAP_8BIT);
    if (source == NULL) {
        if (ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        JS_PopGCRef(ctx, &owner_ref);
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    source->ops = ops;
    source->opaque = opaque;
    source->read_leases = 0;
    source->closed = false;
    JS_SetOpaque(ctx, *object, source);

    if (!JS_IsUndefined(*rooted_owner) &&
        JS_IsException(JS_SetPropertyStr(ctx, *object, ESP32_MQUICKJS_BYTE_SPAN_SOURCE_OWNER_KEY, *rooted_owner))) {
        JS_SetOpaque(ctx, *object, NULL);
        if (ops->destroy != NULL) {
            ops->destroy(ctx, opaque);
        }
        heap_caps_free(source);
        JS_PopGCRef(ctx, &owner_ref);
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }

    JS_PopGCRef(ctx, &owner_ref);
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

bool esp32_mquickjs_byte_view_is_open(JSContext *ctx, JSValue value)
{
    esp32_mquickjs_byte_view_t *view;

    if (ctx == NULL || JS_GetClassID(ctx, value) != JS_CLASS_BYTE_VIEW) {
        return false;
    }
    view = JS_GetOpaque(ctx, value);
    return view != NULL && !view->closed;
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
    if (view->read_leases != 0) {
        JS_ThrowInternalError(ctx, "ByteView update failed because the ByteView is busy");
        return false;
    }
    view->data = data;
    view->length = length;
    return true;
}

bool esp32_mquickjs_byte_view_acquire_read(JSContext *ctx,
                                           JSValue value,
                                           const char *api_name,
                                           const uint8_t **out_data,
                                           size_t *out_length)
{
    esp32_mquickjs_byte_view_t *view;

    if (out_data == NULL || out_length == NULL) {
        return false;
    }
    view = byte_view_from_value(ctx, value, api_name);
    if (view == NULL) {
        return false;
    }
    if (view->read_leases == UINT16_MAX) {
        JS_ThrowInternalError(ctx, "%s could not acquire a ByteView read lease",
                              api_name);
        return false;
    }
    ++view->read_leases;
    *out_data = view->data;
    *out_length = view->length;
    return true;
}

void esp32_mquickjs_byte_view_release_read(JSContext *ctx, JSValue value)
{
    esp32_mquickjs_byte_view_t *view;

    if (JS_GetClassID(ctx, value) != JS_CLASS_BYTE_VIEW) {
        return;
    }
    view = JS_GetOpaque(ctx, value);
    if (view != NULL && view->read_leases != 0) {
        --view->read_leases;
    }
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

static void byte_view_release(esp32_mquickjs_byte_view_t *view)
{
    if (view == NULL) {
        return;
    }
    heap_caps_free(view->owned_data);
    view->owned_data = NULL;
    view->data = NULL;
    view->length = 0;
    view->closed = true;
}

JSValue js_byte_view_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_byte_view_t *view;
    JSValue owner_result;

    (void)argc;
    (void)argv;
    if (JS_GetClassID(ctx, *this_val) != JS_CLASS_BYTE_VIEW) {
        return JS_ThrowTypeError(ctx, "ByteView.close() expects a ByteView");
    }
    view = JS_GetOpaque(ctx, *this_val);
    if (view == NULL) {
        return JS_TRUE;
    }
    if (view->read_leases != 0) {
        return JS_ThrowInternalError(
            ctx, "ByteView.close() failed because the ByteView is busy");
    }

    owner_result = JS_SetPropertyStr(ctx, *this_val,
                                     ESP32_MQUICKJS_BYTE_VIEW_OWNER_KEY,
                                     JS_UNDEFINED);
    JS_SetOpaque(ctx, *this_val, NULL);
    byte_view_release(view);
    heap_caps_free(view);
    return JS_IsException(owner_result) ? JS_EXCEPTION : JS_TRUE;
}

JSValue js_byte_span_source_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_byte_span_source_object_t *source;
    int class_id;
    JSValue owner_result;

    (void)argc;
    (void)argv;
    class_id = JS_GetClassID(ctx, *this_val);
    if (class_id != JS_CLASS_BYTE_SPAN_SOURCE &&
        class_id != JS_CLASS_BITMAP_SPAN_SOURCE) {
        return JS_ThrowTypeError(ctx, "ByteSpanSource.close() expects a ByteSpanSource");
    }
    source = JS_GetOpaque(ctx, *this_val);
    if (source == NULL) {
        return JS_TRUE;
    }
    if (source->read_leases != 0) {
        return JS_ThrowInternalError(
            ctx, "ByteSpanSource.close() failed because the source is busy");
    }
    source->closed = true;
    if (source->ops != NULL && source->ops->destroy != NULL) {
        source->ops->destroy(ctx, source->opaque);
    }
    source->opaque = NULL;
    source->ops = NULL;
    owner_result = JS_SetPropertyStr(ctx, *this_val,
                                     ESP32_MQUICKJS_BYTE_SPAN_SOURCE_OWNER_KEY,
                                     JS_UNDEFINED);
    JS_SetOpaque(ctx, *this_val, NULL);
    heap_caps_free(source);
    return JS_IsException(owner_result) ? JS_EXCEPTION : JS_TRUE;
}

JSValue js_byte_span_source_get_length(JSContext *ctx,
                                       JSValue *this_val,
                                       int argc,
                                       JSValue *argv)
{
    size_t length = 0;

    (void)argc;
    (void)argv;
    if (!esp32_mquickjs_byte_span_source_known_length(ctx, *this_val, &length)) {
        if (JS_HasException(ctx)) {
            return JS_EXCEPTION;
        }
        return JS_NULL;
    }
    return JS_NewUint32(ctx, (uint32_t)length);
}

JSValue js_bitmap_span_source_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "BitmapSpanSource cannot be constructed directly");
}

void js_byte_view_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_byte_view_t *view = opaque;

    (void)ctx;

    if (view == NULL) {
        return;
    }
    byte_view_release(view);
    heap_caps_free(view);
}

void js_byte_span_source_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_byte_span_source_object_t *source = opaque;

    if (source == NULL) {
        return;
    }
    if (!source->closed && source->ops != NULL && source->ops->destroy != NULL) {
        source->ops->destroy(ctx, source->opaque);
    }
    source->closed = true;
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

JSValue js_bitmap_span_source_set_rect(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_byte_span_source_object_t *source;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    source = byte_span_source_from_value(ctx, *this_val, "BitmapSpanSource.setRect()");
    if (source == NULL) {
        return JS_EXCEPTION;
    }
    if (argc < 4 ||
        !js_value_to_i32(ctx, argv[0], &x) ||
        !js_value_to_i32(ctx, argv[1], &y) ||
        !js_value_to_i32(ctx, argv[2], &width) ||
        !js_value_to_i32(ctx, argv[3], &height)) {
        return JS_ThrowTypeError(ctx, "BitmapSpanSource.setRect(x, y, width, height) expects integers");
    }
    if (source->ops == NULL || source->ops->set_rect == NULL) {
        return JS_ThrowTypeError(ctx, "BitmapSpanSource.setRect() is not supported by this source");
    }
    if (source->read_leases != 0) {
        return JS_ThrowInternalError(
            ctx, "BitmapSpanSource.setRect() failed because the source is busy");
    }
    if (!source->ops->set_rect(ctx, source->opaque, x, y, width, height)) {
        return JS_EXCEPTION;
    }
    return *this_val;
}
