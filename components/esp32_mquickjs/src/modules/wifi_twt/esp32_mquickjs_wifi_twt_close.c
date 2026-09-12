#include "esp32_mquickjs_wifi_twt.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_agreement_radio.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_twt_close_group_t group;
    uint32_t timeout_ms;
    bool started, cancelled;
};
static bool close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    (void)self;
    *output = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "wifi.twt.closeAll expects optional options"); return false; }
    uint32_t timeout = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS;
    if (argc == 1 && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val, "wifi.twt.closeAll", keys, 1)) return false;
        JSGCRef ref;
        JSValue *value = JS_PushGCRef(ctx, &ref);
        *value = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
        bool valid = !JS_IsException(*value) && (JS_IsUndefined(*value) ||
            esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS, &timeout));
        JS_PopGCRef(ctx, &ref);
        if (!valid) {
            if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "timeoutMs must be an integer from 1 to 60000");
            return false;
        }
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); return false; }
    state->timeout_ms = timeout;
    *output = state;
    return true;
}
static bool close_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    if (!state->started && !state->cancelled) {
        esp32_mquickjs_wifi_radio_twt_close_agreements(&state->group);
        state->started = true;
        (void)esp32_mquickjs_wifi_twt_agreement_service();
    }
    return true;
}
static esp32_mquickjs_future_poll_t close_poll(esp32_mquickjs_future_driver_state_t *state)
{
    (void)esp32_mquickjs_wifi_twt_agreement_service();
    return state->cancelled || (state->started && !esp32_mquickjs_wifi_radio_twt_close_pending(&state->group))
        ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue close_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{ (void)ctx; (void)state; return JS_UNDEFINED; }
static esp32_mquickjs_cancel_result_t close_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    /* Only the public wait ends. Every selected owner keeps its close request,
     * storage and lease; the shared cleanup worker does not borrow this state. */
    state->cancelled = true;
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static JSValue close_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t ms)
{
    (void)ms;
    state->cancelled = true;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "started", JS_NewBool(state->started)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "selected", JS_NewUint32(ctx,
            state->group.individual_count + state->group.broadcast_count)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "pending", JS_NewUint32(ctx,
            esp32_mquickjs_wifi_radio_twt_close_pending(&state->group)))) {
        JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_TWT_CLOSE_TIMEOUT", "wifi.twt.closeAll",
        "TWT agreement cleanup is still pending; inspect wifi.twt.agreements()", *details);
    JS_PopGCRef(ctx, &ref);
    return result;
}
static void close_destroy(esp32_mquickjs_future_driver_state_t *state) { esp32_mquickjs_memory_payload_free(state); }
static uint32_t close_timeout_ms(const esp32_mquickjs_future_driver_state_t *state) { return state->timeout_ms; }
static esp32_mquickjs_resource_key_t close_resource(const esp32_mquickjs_future_driver_state_t *state)
{ (void)state; return NULL; }
static const esp32_mquickjs_future_driver_t s_twt_close_driver = {
    .memory_owner = "wireless.future", .capture = close_capture, .start = close_start, .poll = close_poll, .finish = close_finish,
    .cancel = close_cancel, .destroy = close_destroy, .timeout_ms = close_timeout_ms,
    .on_timeout = close_timeout, .resource_key = close_resource,
};
JSValue js_wifi_twt_close_all(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "twt");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "closeAll");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *module, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return result;
}
bool esp32_mquickjs_init_wifi_twt_close_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, JSValue *module)
{
    JSGCRef ref;
    JSValue *method = JS_PushGCRef(ctx, &ref);
    *method = JS_GetPropertyStr(ctx, *module, "closeAll");
    bool ok = !JS_IsException(*method) && esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_twt_close_driver);
    JS_PopGCRef(ctx, &ref);
    return ok;
}
#endif
