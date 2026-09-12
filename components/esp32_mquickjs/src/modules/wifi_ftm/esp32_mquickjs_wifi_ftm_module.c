#include "esp32_mquickjs_wifi_ftm.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && (CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT || (CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT))
#include "esp32_mquickjs_wifi_ftm_session.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_options.h"
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

JSValue js_wifi_ftm_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.ftm.capabilities expects no arguments");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-ftm/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
#if CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT && (CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C5)
    SET(result, "recovery", JS_TRUE);
#else
    SET(result, "recovery", JS_FALSE);
#endif
    #if CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    SET(result, "initiator", JS_TRUE);
#else
    SET(result, "initiator", JS_FALSE);
#endif
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
    SET(result, "responderConfiguration", JS_TRUE);
    SET(result, "responderOffset", JS_TRUE);
#else
    SET(result, "responderConfiguration", JS_FALSE);
    SET(result, "responderOffset", JS_FALSE);
#endif
#if CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    SET(result, "maximumOperations", JS_NewUint32(ctx, 1));
    SET(result, "maximumHandles", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_FTM_MAX_HANDLES));
    SET(result, "maximumReportEntries", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_FTM_MAX_REPORT_ENTRIES));
    SET(result, "maximumRetainedEntries", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_FTM_MAX_RETAINED_ENTRIES));
#else
    SET(result, "maximumOperations", JS_NewUint32(ctx, 0));
    SET(result, "maximumHandles", JS_NewUint32(ctx, 0));
    SET(result, "maximumReportEntries", JS_NewUint32(ctx, 0));
    SET(result, "maximumRetainedEntries", JS_NewUint32(ctx, 0));
#endif
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}


#if CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
static JSValue ftm_offset_to_js(JSContext *ctx, const esp32_mquickjs_wifi_ftm_offset_state_t *state)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "radioGeneration", state->generation ? JS_NewUint32(ctx, state->generation) : JS_NULL);
    SET(result, "revision", JS_NewUint32(ctx, state->revision));
    SET(result, "acceptedRevision", state->accepted_revision ? JS_NewUint32(ctx, state->accepted_revision) : JS_NULL);
    SET(result, "requestedCm", state->revision ? JS_NewInt32(ctx, state->requested_cm) : JS_NULL);
    SET(result, "lastAcceptedCm", state->configured ? JS_NewInt32(ctx, state->accepted_cm) : JS_NULL);
    SET(result, "valueCm", state->known ? JS_NewInt32(ctx, state->accepted_cm) : JS_NULL);
    SET(result, "configured", JS_NewBool(state->configured));
    SET(result, "known", JS_NewBool(state->known));
    SET(result, "uncertain", JS_NewBool(state->uncertain));
    SET(result, "error", state->error ? JS_NewInt32(ctx, state->error) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_ftm_responder_offset_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.ftm.responderOffsetStatus expects no arguments");
    esp32_mquickjs_wifi_ftm_offset_state_t state;
    esp32_mquickjs_wifi_radio_ftm_offset_status(&state);
    return ftm_offset_to_js(ctx, &state);
}
JSValue js_wifi_ftm_set_responder_offset(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    int32_t centimeters;
    if (argc != 1 || !esp32_mquickjs_value_to_bounded_i32(ctx, argv[0], INT16_MIN, INT16_MAX, &centimeters))
        return JS_ThrowTypeError(ctx, "FTM responder offset expects an integer from -32768 to 32767 cm");
    esp32_mquickjs_wifi_ftm_offset_state_t state;
    esp32_mquickjs_wifi_radio_config_result_t result;
    esp_err_t err = esp32_mquickjs_wifi_radio_write_ftm_offset(centimeters, &state, &result);
    if (err == ESP_OK) return ftm_offset_to_js(ctx, &state);
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, err));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(err)));
    SET(details, "stage", JS_NewString(ctx, result.stage));
    SET(details, "mutationAttempted", JS_NewBool(result.mutation_attempted));
    SET(details, "offset", ftm_offset_to_js(ctx, &state));
    (void)esp32_mquickjs_throw_native_error(ctx, "WIFI_FTM_OFFSET_FAILED", "wifi.ftm.setResponderOffsetCm",
        "FTM responder offset was not confirmed; inspect responderOffsetStatus()", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
#endif
#endif
