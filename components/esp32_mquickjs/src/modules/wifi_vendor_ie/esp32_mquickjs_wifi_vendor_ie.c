#include "esp32_mquickjs_wifi_vendor_ie_watch.h"
#include "esp32_mquickjs_wifi_vendor_ie.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_options.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include <string.h>

static const char *const s_vendor_frames[] = {
    "beacon", "probe-request", "probe-response", "association-request", "association-response"
};

static bool vendor_choice(JSContext *ctx, JSValue value, const char *const *names, unsigned count, unsigned *out)
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (text == NULL) return false;
    for (unsigned i = 0; i < count; ++i) {
        if (length == strlen(names[i]) && memcmp(text, names[i], length) == 0) { *out = i; return true; }
    }
    return false;
}

static bool vendor_interface(JSContext *ctx, JSValue value, unsigned *out)
{
    static const char *const names[] = {"station", "access-point"};
    return vendor_choice(ctx, value, names, 2, out);
}

/* Copy into a fixed native span before the SDK call. Array accessors can move
 * GC storage or throw; only the root is used between such accesses. */
static bool vendor_bytes(JSContext *ctx, JSGCRef *input, uint8_t *bytes, size_t *length)
{
    JSGCRef field_ref;
    JSValue *field = JS_PushGCRef(ctx, &field_ref);
    bool ok = false;
    if (JS_GetClassID(ctx, input->val) == JS_CLASS_BYTE_VIEW) {
        const uint8_t *source = NULL;
        size_t count = 0;
        if (!esp32_mquickjs_byte_view_acquire_read(ctx, input->val, "wifi.vendorIe.set", &source, &count)) goto done;
        if (count >= 6 && count <= ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES) {
            memcpy(bytes, source, count); *length = count; ok = true;
        }
        esp32_mquickjs_byte_view_release_read(ctx, input->val);
        if (!ok) goto invalid;
    } else {
        if (JS_GetClassID(ctx, input->val) < 0) goto invalid;
        *field = JS_GetPropertyStr(ctx, input->val, "length");
        if (JS_IsException(*field)) goto done;
        uint32_t count;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 6, ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES, &count)) goto invalid;
        for (uint32_t i = 0; i < count; ++i) {
            *field = JS_GetPropertyUint32(ctx, input->val, i);
            if (JS_IsException(*field)) goto done;
            uint32_t byte;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0, 255, &byte)) goto invalid;
            bytes[i] = (uint8_t)byte;
        }
        *length = count;
        ok = true;
    }
    if (bytes[0] != WIFI_VENDOR_IE_ELEMENT_ID || (size_t)bytes[1] + 2U != *length) goto invalid;
    goto done;
invalid:
    ok = false;
    JS_ThrowTypeError(ctx, "Vendor IE data must contain one complete 6-257 byte element (0xdd, length, OUI, type, payload)");
done:
    JS_PopGCRef(ctx, &field_ref);
    return ok;
}

JSValue esp32_mquickjs_wifi_vendor_ie_status(JSContext *ctx)
{
    esp32_mquickjs_wifi_vendor_ie_status_t status;
    esp32_mquickjs_wifi_radio_vendor_ie_status(&status);
    JSGCRef result_ref, slots_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *slots = JS_PushGCRef(ctx, &slots_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    *slots = JS_NewArray(ctx, 0);
    if (JS_IsException(*result) || JS_IsException(*slots)) goto fail;
#define SET(obj, key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, obj, key, value)) goto fail; } while (0)
    uint32_t payload_bound = 0, output_index = 0;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS; ++i) {
        const esp32_mquickjs_wifi_vendor_ie_slot_t *slot = &status.slots[i];
        unsigned frame = i / 2U;
        bool station = frame == WIFI_VND_IE_TYPE_PROBE_REQ || frame == WIFI_VND_IE_TYPE_ASSOC_REQ;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (!station) continue;
#endif
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item)) goto fail;
        SET(item, "interface", JS_NewString(ctx, station ? "station" : "access-point"));
        SET(item, "frame", JS_NewString(ctx, s_vendor_frames[frame]));
        SET(item, "index", JS_NewInt32(ctx, i % 2U));
        SET(item, "state", JS_NewString(ctx, slot->pending ? "uncertain" : slot->length ? "enabled" : "empty"));
        SET(item, "byteLength", slot->pending ? JS_NULL : JS_NewInt32(ctx, slot->length));
        SET(item, "espCode", slot->error != ESP_OK ? JS_NewInt32(ctx, slot->error) : JS_NULL);
        payload_bound += slot->pending ? ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES : slot->length;
        if (JS_IsException(JS_SetPropertyUint32(ctx, *slots, output_index++, *item))) goto fail;
    }
    SET(result, "radioGeneration", JS_NewUint32(ctx, status.generation));
    SET(result, "owners", JS_NewUint32(ctx, status.owners));
    SET(result, "startPending", JS_NewBool(status.start_pending));
    SET(result, "driverPayloadBytesUpperBound", JS_NewUint32(ctx, payload_bound));
    SET(result, "slots", *slots);
    SET(result, "watch", esp32_mquickjs_wifi_vendor_ie_watch_status(ctx));
#undef SET
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &slots_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &slots_ref); JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

static JSValue vendor_error(JSContext *ctx, const char *operation, esp_err_t err)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, err)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(err))) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "status", esp32_mquickjs_wifi_vendor_ie_status(ctx))) {
        JS_PopGCRef(ctx, &details_ref); return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_VENDOR_IE_FAILED", operation,
        "Wi-Fi Vendor IE operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

JSValue js_wifi_vendor_ie_set(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.vendorIe.set expects one options object");
    static const char *const keys[] = {"interface", "frame", "index", "enabled", "data"};
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref);
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *options = argv[0];
    uint8_t bytes[ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES];
    size_t length = 0;
    unsigned interface, frame;
    uint32_t index;
    bool enabled;
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.vendorIe.set", keys, 5)) goto fail;
    *value = JS_GetPropertyStr(ctx, *options, "interface");
    if (JS_IsException(*value)) goto fail;
    if (!vendor_interface(ctx, *value, &interface)) goto invalid;
    *value = JS_GetPropertyStr(ctx, *options, "frame");
    if (JS_IsException(*value)) goto fail;
    if (!vendor_choice(ctx, *value, s_vendor_frames, 5, &frame)) goto invalid;
    if ((frame == WIFI_VND_IE_TYPE_PROBE_REQ || frame == WIFI_VND_IE_TYPE_ASSOC_REQ) != (interface == WIFI_IF_STA)) goto invalid;
    *value = JS_GetPropertyStr(ctx, *options, "index");
    if (JS_IsException(*value)) goto fail;
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 1, &index)) goto invalid;
    *value = JS_GetPropertyStr(ctx, *options, "enabled");
    if (JS_IsException(*value)) goto fail;
    if (!JS_IsBool(*value)) goto invalid;
    enabled = *value == JS_TRUE;
    *value = JS_GetPropertyStr(ctx, *options, "data");
    if (JS_IsException(*value)) goto fail;
    if (enabled) { if (!vendor_bytes(ctx, &value_ref, bytes, &length)) goto fail; }
    else if (!JS_IsUndefined(*value)) goto invalid;
    JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    esp_err_t err = esp32_mquickjs_wifi_radio_vendor_ie_set((wifi_interface_t)interface,
        (wifi_vendor_ie_type_t)frame, index, enabled, enabled ? bytes : NULL, length);
    if (err != ESP_OK) return vendor_error(ctx, "wifi.vendorIe.set", err);
    return esp32_mquickjs_wifi_vendor_ie_status(ctx);
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Invalid Vendor IE interface/frame/index/enabled/data options");
fail:
    JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_vendor_ie_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.vendorIe.clear expects an optional interface");
    int interface = -1;
    if (argc && !JS_IsUndefined(argv[0])) {
        unsigned selected;
        if (!vendor_interface(ctx, argv[0], &selected))
            return JS_HasException(ctx) ? JS_EXCEPTION : JS_ThrowTypeError(ctx, "Invalid Vendor IE interface");
        interface = selected;
    }
    esp_err_t err = esp32_mquickjs_wifi_radio_vendor_ie_clear(interface);
    return err == ESP_OK ? JS_UNDEFINED : vendor_error(ctx, "wifi.vendorIe.clear", err);
}

JSValue js_wifi_vendor_ie_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "wifi.vendorIe.status expects no arguments");
    return esp32_mquickjs_wifi_vendor_ie_status(ctx);
}
#endif
