#include "esp32_mquickjs_wifi_roaming.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT || CONFIG_ESP_WIFI_11R_SUPPORT)
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_options.h"
#include <string.h>
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

JSValue js_wifi_roaming_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.roaming.capabilities expects no arguments");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-roaming/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
#if CONFIG_ESP_WIFI_RRM_SUPPORT
    SET(result, "rrm11k", JS_TRUE);
#else
    SET(result, "rrm11k", JS_FALSE);
#endif
#if CONFIG_ESP_WIFI_WNM_SUPPORT
    SET(result, "btm11v", JS_TRUE);
    SET(result, "maximumBtmCandidates", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_BTM_MAX_CANDIDATES));
#else
    SET(result, "btm11v", JS_FALSE);
    SET(result, "maximumBtmCandidates", JS_NewUint32(ctx, 0));
#endif
#if CONFIG_ESP_WIFI_11R_SUPPORT
    SET(result, "fastTransition11r", JS_TRUE);
#else
    SET(result, "fastTransition11r", JS_FALSE);
#endif
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

#if CONFIG_ESP_WIFI_RRM_SUPPORT || CONFIG_ESP_WIFI_WNM_SUPPORT
static JSValue roaming_error(JSContext *ctx, const char *operation, const esp32_mquickjs_wifi_roaming_result_t *r)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "stage", JS_NewString(ctx, r->stage));
    SET(details, "espCode", JS_NewInt32(ctx, r->error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(r->error)));
    SET(details, "radioGeneration", r->radio_generation ? JS_NewUint32(ctx, r->radio_generation) : JS_NULL);
    SET(details, "nativeEntered", JS_NewBool(r->entered));
    SET(details, "submissionAttempted", JS_NewBool(r->submitted));
    SET(details, "sdkCode", r->submitted ? JS_NewInt32(ctx, r->sdk_code) : JS_NULL);
    (void)esp32_mquickjs_throw_native_error(ctx, "WIFI_ROAMING_FAILED", operation,
        "Wi-Fi roaming operation failed; a submitted query cannot be recalled", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue roaming_supported(JSContext *ctx, int argc, bool rrm)
{
    const char *operation = rrm ? "wifi.roaming.isRrmSupported" : "wifi.roaming.isBtmSupported";
    if (argc != 0) return JS_ThrowTypeError(ctx, "Wi-Fi roaming support queries expect no arguments");
    esp32_mquickjs_wifi_roaming_result_t r;
    if (esp32_mquickjs_wifi_roaming_execute(NULL, &r) != ESP_OK) return roaming_error(ctx, operation, &r);
    return JS_NewBool(rrm ? r.rrm : r.btm);
}
#if CONFIG_ESP_WIFI_RRM_SUPPORT
JSValue js_wifi_roaming_is_rrm_supported(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ (void)self; (void)argv; return roaming_supported(ctx, argc, true); }
#endif
#if CONFIG_ESP_WIFI_WNM_SUPPORT
JSValue js_wifi_roaming_is_btm_supported(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ (void)self; (void)argv; return roaming_supported(ctx, argc, false); }
#endif
#endif

#if CONFIG_ESP_WIFI_WNM_SUPPORT
static const char *const s_btm_reasons[] = {"unspecified", "frame-loss", "delay", "bandwidth", "load-balance",
    "rssi", "retransmissions", "interference", "gray-zone", "premium-ap"};
static int roaming_hex(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static bool roaming_mac(JSContext *ctx, JSValue value, uint8_t mac[6])
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer; size_t length;
    const char *s = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (s == NULL || length != 17) return false;
    for (unsigned i = 0; i < 6; ++i) {
        int high = roaming_hex(s[3*i]), low = roaming_hex(s[3*i+1]);
        if (high < 0 || low < 0 || (i < 5 && s[3*i+2] != ':')) return false;
        mac[i] = (uint8_t)((high << 4) | low);
    }
    return true; /* Native encoder rejects multicast/zero/duplicate addresses. */
}
static bool roaming_capture(JSContext *ctx, JSValue input, esp32_mquickjs_wifi_btm_query_t *q)
{
    static const char *const keys[] = {"reason", "candidates", "allowApChannelChange"};
    static const char *const candidate_keys[] = {"bssid", "bssidInformation", "operatingClass", "channel", "phyType", "preference"};
    JSGCRef options_ref, list_ref, item_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *list = JS_PushGCRef(ctx, &list_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref), *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    bool ok = false;
    memset(q, 0, sizeof(*q));
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.roaming.sendBtmQuery", keys, 3)) goto done;
    *value = JS_GetPropertyStr(ctx, *options, "reason");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!JS_IsString(ctx, *value)) goto invalid;
        JSCStringBuf buffer; size_t length;
        const char *name = JS_ToCStringLen(ctx, &length, *value, &buffer);
        if (name == NULL) goto done;
        unsigned i;
        for (i = 0; i < 10; ++i) if (length == strlen(s_btm_reasons[i]) && !memcmp(name, s_btm_reasons[i], length)) break;
        if (i == 10) goto invalid;
        q->reason = (uint8_t)i;
    }
    *value = JS_GetPropertyStr(ctx, *options, "allowApChannelChange");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value)) {
        if (!JS_IsBool(*value)) goto invalid;
        q->allow_ap_channel_change = *value == JS_TRUE;
    }
    *list = JS_GetPropertyStr(ctx, *options, "candidates");
    if (JS_IsException(*list)) goto done;
    if (!JS_IsUndefined(*list)) {
        if (!JS_IsArray(ctx, *list)) goto invalid;
        *value = JS_GetPropertyStr(ctx, *list, "length");
        if (JS_IsException(*value)) goto done;
        uint32_t count;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, ESP32_MQUICKJS_WIFI_BTM_MAX_CANDIDATES, &count)) goto invalid;
        q->count = (uint8_t)count;
        for (unsigned i = 0; i < count; ++i) {
            *item = JS_GetPropertyUint32(ctx, *list, i);
            if (JS_IsException(*item)) goto done;
            if (!esp32_mquickjs_validate_plain_options(ctx, *item, "BTM candidate", candidate_keys, 6)) goto done;
            esp32_mquickjs_wifi_btm_candidate_t *c = &q->candidates[i];
            *value = JS_GetPropertyStr(ctx, *item, "bssid");
            if (JS_IsException(*value)) goto done;
            if (!roaming_mac(ctx, *value, c->bssid)) goto invalid;
            uint32_t v;
#define NUMBER(field, min, max, dest) do { \
    *value = JS_GetPropertyStr(ctx, *item, field); \
    if (JS_IsException(*value)) goto done; \
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, min, max, &v)) goto invalid; \
    dest = v; \
} while (0)
            NUMBER("bssidInformation", 0, UINT32_MAX, c->information);
            NUMBER("operatingClass", 1, 255, c->operating_class);
            NUMBER("channel", 1, 233, c->channel);
            NUMBER("phyType", 0, 255, c->phy_type);
#undef NUMBER
            *value = JS_GetPropertyStr(ctx, *item, "preference");
            if (JS_IsException(*value)) goto done;
            if (!JS_IsUndefined(*value)) {
                if (!esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, 255, &v)) goto invalid;
                c->preference_set = true; c->preference = (uint8_t)v;
            }
        }
    }
    char encoded[ESP32_MQUICKJS_WIFI_BTM_TEXT_BYTES];
    if (!esp32_mquickjs_wifi_btm_encode(q, encoded, sizeof(encoded))) goto invalid;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Invalid BTM reason or candidate; expected at most 16 distinct unicast BSSIDs and bounded integer fields");
done:
    JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &list_ref); JS_PopGCRef(ctx, &options_ref);
    return ok;
}
JSValue js_wifi_roaming_send_btm_query(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.roaming.sendBtmQuery expects one options object");
    esp32_mquickjs_wifi_btm_query_t q;
    if (!roaming_capture(ctx, argv[0], &q)) return JS_EXCEPTION;
    /* Allocate the result before mutation, retaining it across native dispatch. */
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "accepted", JS_TRUE);
    SET(result, "reason", JS_NewString(ctx, s_btm_reasons[q.reason]));
    SET(result, "candidateCount", JS_NewUint32(ctx, q.count));
    SET(result, "completion", JS_NewString(ctx, "sdk-submit"));
    esp32_mquickjs_wifi_roaming_result_t r;
    if (esp32_mquickjs_wifi_roaming_execute(&q, &r) != ESP_OK) {
        JS_PopGCRef(ctx, &ref); return roaming_error(ctx, "wifi.roaming.sendBtmQuery", &r);
    }
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
#endif
#undef SET
#endif
