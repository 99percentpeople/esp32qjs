#include "esp32_mquickjs_wifi_twt.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_radio.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include <stdatomic.h>

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_twt_broadcast_snapshot_t snapshot;
    uint32_t generation, timeout_ms;
    esp_err_t error;
    bool submitted;
    atomic_bool worker_done, cancelled;
};
#define SET(object, key, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, key, value)) goto fail; } while (0)

static JSValue broadcast_error(JSContext *ctx, esp_err_t error, bool timeout)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "stage", JS_NewString(ctx, timeout ? "deadline" : "snapshot"));
    JSValue result = esp32_mquickjs_throw_native_error(ctx,
        timeout ? "WIFI_TWT_DISCOVERY_TIMEOUT" : "WIFI_TWT_DISCOVERY_FAILED",
        "wifi.twt.broadcasts", "broadcast TWT discovery failed", *details);
    JS_PopGCRef(ctx, &ref);
    return result;
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static bool broadcast_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    (void)self;
    *output = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "wifi.twt.broadcasts expects optional options"); return false; }
    uint32_t timeout = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS;
    if (argc == 1 && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val, "wifi.twt.broadcasts", keys, 1)) return false;
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
    atomic_init(&state->worker_done, false);
    atomic_init(&state->cancelled, false);
    *output = state;
    return true;
}
static void broadcast_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    if (!atomic_load_explicit(&state->cancelled, memory_order_acquire))
        state->error = esp32_mquickjs_wifi_radio_twt_broadcast_snapshot(&state->snapshot, &state->generation);
    /* Last access. Future retirement keeps the storage alive through this
     * publication, including timeout/cancel/runtime teardown. No JS roots. */
    atomic_store_explicit(&state->worker_done, true, memory_order_release);
}
static void broadcast_schedule(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state->submitted && !atomic_load_explicit(&state->cancelled, memory_order_acquire))
        state->submitted = esp32_mquickjs_submit_background_worker(broadcast_worker, state);
}
static bool broadcast_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    broadcast_schedule(state);
    return true;
}
static esp32_mquickjs_future_poll_t broadcast_poll(esp32_mquickjs_future_driver_state_t *state)
{
    broadcast_schedule(state);
    if (state->submitted && !atomic_load_explicit(&state->worker_done, memory_order_acquire))
        return ESP32_MQUICKJS_FUTURE_PENDING;
    return state->submitted || atomic_load_explicit(&state->cancelled, memory_order_acquire) ?
        ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue broadcast_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return broadcast_error(ctx, state->error, false);
    JSGCRef result_ref, array_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref), *array = JS_PushGCRef(ctx, &array_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "radioGeneration", JS_NewUint32(ctx, state->generation));
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    for (unsigned i = 0; i < state->snapshot.count; ++i) {
        const esp_wifi_btwt_info_t *schedule = &state->snapshot.schedules[i];
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item)) goto fail;
        SET(item, "broadcastId", JS_NewUint32(ctx, schedule->btwt_info_id));
        SET(item, "joined", JS_NewBool((state->snapshot.joined_bitmap & (UINT32_C(1) << schedule->btwt_info_id)) != 0U));
        SET(item, "trigger", JS_NewBool(schedule->btwt_trigger));
        SET(item, "announced", JS_NewBool(!schedule->btwt_flow_type));
        SET(item, "recommendation", JS_NewUint32(ctx, schedule->btwt_recommendation));
        SET(item, "minimumWakeDuration", JS_NewUint32(ctx, schedule->btwt_wake_duration));
        /* The SDK getter copies the frame's duration byte, not its unit bit.
         * Do not interpret the misleading header comment as a unit value. */
        SET(item, "wakeDurationUnit", JS_NULL);
        SET(item, "wakeIntervalMantissa", JS_NewUint32(ctx, schedule->btwt_wake_interval_mantissa));
        SET(item, "wakeIntervalExponent", JS_NewUint32(ctx, schedule->btwt_wake_interval_exponent));
        uint64_t interval = (uint64_t)schedule->btwt_wake_interval_mantissa << schedule->btwt_wake_interval_exponent;
        SET(item, "wakeIntervalUs", JS_NewFloat64(ctx, (double)interval));
        SET(item, "persistence", JS_NewUint32(ctx, schedule->btwt_info_persistence));
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array, i, *item))) goto fail;
    }
    SET(result, "schedules", *array);
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &array_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &array_ref); JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}
static esp32_mquickjs_cancel_result_t broadcast_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->cancelled, true, memory_order_release);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static JSValue broadcast_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    (void)broadcast_cancel(state);
    return broadcast_error(ctx, ESP_ERR_TIMEOUT, true);
}
static void broadcast_destroy(esp32_mquickjs_future_driver_state_t *state) { esp32_mquickjs_memory_payload_free(state); }
static uint32_t broadcast_timeout_ms(const esp32_mquickjs_future_driver_state_t *state) { return state->timeout_ms; }
static esp32_mquickjs_resource_key_t broadcast_resource(const esp32_mquickjs_future_driver_state_t *state)
{ (void)state; return NULL; }
static const esp32_mquickjs_future_driver_t s_twt_broadcast_driver = {
    .memory_owner = "wireless.future", .capture = broadcast_capture, .start = broadcast_start, .poll = broadcast_poll, .finish = broadcast_finish,
    .cancel = broadcast_cancel, .destroy = broadcast_destroy, .timeout_ms = broadcast_timeout_ms,
    .on_timeout = broadcast_timeout, .resource_key = broadcast_resource,
};
JSValue js_wifi_twt_broadcasts(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "twt");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "broadcasts");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *module, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return result;
}
bool esp32_mquickjs_init_wifi_twt_broadcast_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, JSValue *module)
{
    JSGCRef ref;
    JSValue *method = JS_PushGCRef(ctx, &ref);
    *method = JS_GetPropertyStr(ctx, *module, "broadcasts");
    bool ok = !JS_IsException(*method) && esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_twt_broadcast_driver);
    JS_PopGCRef(ctx, &ref);
    return ok;
}
#endif
