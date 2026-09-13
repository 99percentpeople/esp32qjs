#include "esp32_mquickjs_wifi_frame_filter.h"
#include "esp32_mquickjs_wifi_frame_type.h"
#include "esp32_mquickjs_wifi_rx.h"
#include "esp32_mquickjs_options.h"
#include <string.h>

bool esp32_mquickjs_wifi_frame_filter_parse(JSContext *ctx, JSValue value, uint16_t output[4])
{
    static const char *const keys[] = {"type", "subtype", "name"};
    JSGCRef list_ref, item_ref, value_ref;
    JSValue *list = JS_PushGCRef(ctx, &list_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    JSValue *property = JS_PushGCRef(ctx, &value_ref);
    *list = value; *item = JS_UNDEFINED; *property = JS_UNDEFINED;
    uint16_t masks[4] = {0};
    uint32_t count;
    bool valid = false;
    if (!JS_IsArray(ctx, *list)) goto done;
    *property = JS_GetPropertyStr(ctx, *list, "length");
    if (JS_IsException(*property) || !esp32_mquickjs_value_to_bounded_u32(ctx, *property, 0, 64, &count)) goto done;
    for (uint32_t i = 0; i < count; ++i) {
        *item = JS_GetPropertyUint32(ctx, *list, i);
        if (JS_IsException(*item) || !esp32_mquickjs_validate_plain_options(ctx, *item, "filter.frames item", keys, 3)) goto done;
        uint32_t type, subtype;
        *property = JS_GetPropertyStr(ctx, *item, "type");
        if (JS_IsException(*property) || !esp32_mquickjs_value_to_bounded_u32(ctx, *property, 0, 3, &type)) goto done;
        *property = JS_GetPropertyStr(ctx, *item, "subtype");
        if (JS_IsException(*property) || !esp32_mquickjs_value_to_bounded_u32(ctx, *property, 0, 15, &subtype)) goto done;
        *property = JS_GetPropertyStr(ctx, *item, "name");
        if (JS_IsException(*property)) goto done;
        if (!JS_IsUndefined(*property)) {
            const char *name = esp32_mquickjs_wifi_rx_subtype_name((esp32_mquickjs_wifi_packet_type_t)type, subtype);
            size_t choice;
            if (name == NULL ? !JS_IsNull(*property) :
                !esp32_mquickjs_value_to_enum(ctx, *property, &name, 1, &choice)) goto done;
        }
        uint16_t bit = (uint16_t)(1U << subtype);
        if ((masks[type] & bit) != 0) goto done;
        masks[type] |= bit;
    }
    memcpy(output, masks, sizeof(masks));
    valid = true;
done:
    JS_PopGCRef(ctx, &value_ref);
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &list_ref);
    if (!valid && !JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Invalid filter.frames");
    return valid;
}

JSValue esp32_mquickjs_wifi_frame_filter_to_js(JSContext *ctx, const uint16_t masks[4])
{
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *item = JS_UNDEFINED;
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    uint32_t index = 0;
    for (unsigned type = 0; type < 4; ++type) {
        for (unsigned subtype = 0; subtype < 16; ++subtype) {
            if (!(masks[type] & (1U << subtype))) continue;
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
