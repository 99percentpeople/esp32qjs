#include "esp32_mquickjs_wifi_frame_type.h"
#include "esp32_mquickjs_wifi_rx.h"
#include "esp32_mquickjs_core.h"

JSValue esp32_mquickjs_wifi_frame_type_to_js(JSContext *ctx, unsigned type, unsigned subtype)
{
    if (type > 3 || subtype > 15) return JS_ThrowInternalError(ctx, "invalid Wi-Fi frame type");
    const char *name = esp32_mquickjs_wifi_rx_subtype_name((esp32_mquickjs_wifi_packet_type_t)type, subtype);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "type", JS_NewUint32(ctx, type)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "subtype", JS_NewUint32(ctx, subtype)) ||
        !esp32_mquickjs_set_property_ref(ctx, result, "name", name ? JS_NewString(ctx, name) : JS_NULL)) {
        JS_PopGCRef(ctx, &ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &ref);
}

JSValue esp32_mquickjs_wifi_frame_types_to_js(JSContext *ctx)
{
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *item = JS_UNDEFINED;
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    uint32_t index = 0;
    for (unsigned type = 0; type < 3; ++type) {
        for (unsigned subtype = 0; subtype < 16; ++subtype) {
            if (esp32_mquickjs_wifi_rx_subtype_name((esp32_mquickjs_wifi_packet_type_t)type, subtype) == NULL) continue;
            *item = esp32_mquickjs_wifi_frame_type_to_js(ctx, type, subtype);
            if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *array, index++, *item))) goto fail;
        }
    }
    JS_PopGCRef(ctx, &item_ref);
    return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &array_ref);
    return JS_EXCEPTION;
}
