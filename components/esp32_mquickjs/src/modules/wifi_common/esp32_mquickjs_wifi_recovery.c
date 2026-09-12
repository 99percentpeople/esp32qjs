#include "esp32_mquickjs_wifi_raw_tx.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_wifi_action.h"
#include "esp32_mquickjs_wifi_ftm.h"
#include "esp32_mquickjs_wifi_twt.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_wifi_wait.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_recovery_t recovery;
    esp32_mquickjs_wifi_recovery_request_t operation;
    uint32_t timeout_ms;
    int64_t deadline_us;
    esp_err_t error;
    bool allow_disconnect, started, cancelled, complete;
};
/* Separate from send's Future lane: the original sender must still be polled
 * to consume physical termination while this recovery waits for its owner. */
static const char s_wifi_recovery_future_lane;

static const char *recovery_method(esp32_mquickjs_wifi_recovery_kind_t kind)
{
    if (kind == ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX) return "wifi.rawTx.recover";
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (kind == ESP32_MQUICKJS_WIFI_RECOVERY_TWT) return "wifi.twt.recover";
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (kind == ESP32_MQUICKJS_WIFI_RECOVERY_FTM) return "wifi.ftm.recover";
#endif
    (void)kind;
    return "wifi.action.recover";
}
static const char *recovery_code(esp32_mquickjs_wifi_recovery_kind_t kind, bool timeout)
{
    if (kind == ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX)
        return timeout ? "WIFI_RAW_TX_RECOVERY_TIMEOUT" : "WIFI_RAW_TX_RECOVERY_FAILED";
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    if (kind == ESP32_MQUICKJS_WIFI_RECOVERY_TWT)
        return timeout ? "WIFI_TWT_RECOVERY_TIMEOUT" : "WIFI_TWT_RECOVERY_FAILED";
#endif
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (kind == ESP32_MQUICKJS_WIFI_RECOVERY_FTM)
        return timeout ? "WIFI_FTM_RECOVERY_TIMEOUT" : "WIFI_FTM_RECOVERY_FAILED";
#endif
    (void)kind;
    return timeout ? "WIFI_ACTION_RECOVERY_TIMEOUT" : "WIFI_ACTION_RECOVERY_FAILED";
}

static bool recovery_whole_generation(esp32_mquickjs_wifi_recovery_kind_t kind)
{
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    return kind == ESP32_MQUICKJS_WIFI_RECOVERY_TWT;
#else
    (void)kind;
    return false;
#endif
}

static bool recovery_capture_kind(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, esp32_mquickjs_wifi_recovery_kind_t kind)
{
    (void)self;
    *output = NULL;
    if (argc != 1) { JS_ThrowTypeError(ctx, "%s expects options", recovery_method(kind)); return false; }
    static const char *const keys[] = {"sequence", "radioGeneration", "allowDisconnect", "timeoutMs"};
    static const char *const generation_keys[] = {"closeAll", "radioGeneration", "allowDisconnect", "timeoutMs"};
    bool whole_generation = recovery_whole_generation(kind);
    if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val, recovery_method(kind),
        whole_generation ? generation_keys : keys, 4)) return false;
    esp32_mquickjs_wifi_recovery_request_t operation = {.kind = kind};
    uint32_t timeout = 10000;
    bool allow_disconnect = false, ok = false;
    JSGCRef ref;
    JSValue *field = JS_PushGCRef(ctx, &ref);
    *field = JS_GetPropertyStr(ctx, argv[0].val, whole_generation ? "closeAll" : "sequence");
    if (JS_IsException(*field)) goto done;
    if (whole_generation) {
        if (*field != JS_TRUE) goto done;
    } else if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, UINT32_MAX, &operation.identity)) goto done;
    *field = JS_GetPropertyStr(ctx, argv[0].val, "radioGeneration");
    if (JS_IsException(*field) || !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, UINT32_MAX, &operation.generation)) goto done;
    *field = JS_GetPropertyStr(ctx, argv[0].val, "allowDisconnect");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto done;
        allow_disconnect = *field == JS_TRUE;
    }
    if (whole_generation && !allow_disconnect) goto done;
    *field = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 60000, &timeout)) goto done;
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); goto done; }
    state->operation = operation;
    state->timeout_ms = timeout;
    state->allow_disconnect = allow_disconnect;
    state->recovery.execution.stage = "recovery-queued";
    *output = state;
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid %s options", recovery_method(kind));
    JS_PopGCRef(ctx, &ref);
    return ok;
}

static bool recovery_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    return recovery_capture_kind(ctx, self, argc, argv, output, ESP32_MQUICKJS_WIFI_RECOVERY_ACTION);
}
static bool raw_tx_recovery_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    return recovery_capture_kind(ctx, self, argc, argv, output, ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX);
}
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
static bool ftm_recovery_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    return recovery_capture_kind(ctx, self, argc, argv, output, ESP32_MQUICKJS_WIFI_RECOVERY_FTM);
}
#endif

#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static bool twt_recovery_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    return recovery_capture_kind(ctx, self, argc, argv, output, ESP32_MQUICKJS_WIFI_RECOVERY_TWT);
}
#endif

static bool recovery_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    state->started = true;
    state->deadline_us = esp_timer_get_time() + (int64_t)state->timeout_ms * 1000;
    return true;
}

static esp32_mquickjs_future_poll_t recovery_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled || state->error != ESP_OK || state->complete) return ESP32_MQUICKJS_FUTURE_READY;
    if (!state->started) return ESP32_MQUICKJS_FUTURE_PENDING;
    int64_t remaining = state->deadline_us - esp_timer_get_time();
    if (remaining <= 0) return ESP32_MQUICKJS_FUTURE_PENDING; /* Future deadline callback owns settlement. */
    state->error = esp32_mquickjs_wifi_wait_begin((uint32_t)((remaining + 999) / 1000));
    if (state->error != ESP_OK) {
        state->recovery.execution.stage = "recovery-wait-admission";
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (!state->recovery.execution.admitted)
        state->error = esp32_mquickjs_wifi_recovery_begin(&state->recovery, &state->operation, state->allow_disconnect);
    else
        state->error = esp32_mquickjs_wifi_recovery_step(&state->recovery, &state->complete);
    esp32_mquickjs_wifi_wait_end();
    return state->error != ESP_OK || state->complete ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}

#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)
static JSValue recovery_error(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, bool timeout)
{
    esp32_mquickjs_wifi_radio_status_t radio = {0};
    (void)esp32_mquickjs_wifi_radio_get_status(&radio);
    esp_err_t error = timeout ? ESP_ERR_TIMEOUT : state->error;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "stage", JS_NewString(ctx, state->recovery.execution.stage ? state->recovery.execution.stage : "recovery-admission"));
    if (!recovery_whole_generation(state->operation.kind)) SET(details, "sequence", JS_NewUint32(ctx, state->operation.identity));
    SET(details, "radioGeneration", JS_NewUint32(ctx, state->operation.generation));
    SET(details, "lifecycleAdmitted", JS_NewBool(state->recovery.execution.admitted));
    SET(details, "checkpointAttempted", JS_NewBool(state->recovery.execution.stop_attempted));
    SET(details, "replayAttempted", JS_NewBool(state->recovery.execution.configuration_attempted));
    SET(details, "resumeAttempted", JS_NewBool(state->recovery.execution.resume_attempted));
    SET(details, "cleanupPending", JS_NewBool(esp32_mquickjs_wifi_configuration_pending()));
    SET(details, "restartRequired", JS_NewBool(radio.restart_required ||
        esp32_mquickjs_wifi_ap_netif_cleanup_error() != ESP_OK || esp32_mquickjs_wifi_state()->sta_detach_error != ESP_OK));
    SET(details, "radioFaultStage", radio.fault_stage ? JS_NewString(ctx, radio.fault_stage) : JS_NULL);
    SET(details, "radioFaultError", radio.fault_stage ? JS_NewInt32(ctx, radio.fault_error) : JS_NULL);
    (void)esp32_mquickjs_throw_native_error(ctx, recovery_code(state->operation.kind, timeout),
        recovery_method(state->operation.kind), "Wi-Fi recovery did not complete; inspect wifi.status() before further work", *details);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

static JSValue recovery_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return recovery_error(ctx, state, false);
    esp32_mquickjs_wifi_radio_status_t radio = {0};
    if (!state->complete || esp32_mquickjs_wifi_radio_get_status(&radio) != ESP_OK) {
        state->error = ESP_ERR_INVALID_STATE;
        return recovery_error(ctx, state, false);
    }
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    if (!recovery_whole_generation(state->operation.kind)) SET(result, "sequence", JS_NewUint32(ctx, state->operation.identity));
    SET(result, "previousRadioGeneration", JS_NewUint32(ctx, state->operation.generation));
    SET(result, "radioGeneration", JS_NewUint32(ctx, radio.generation));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}
static esp32_mquickjs_cancel_result_t recovery_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    return ESP32_MQUICKJS_CANCELLED;
}
static void recovery_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    esp32_mquickjs_wifi_recovery_dispose(&state->recovery);
    esp32_mquickjs_memory_payload_free(state);
}
static uint32_t recovery_timeout(const esp32_mquickjs_future_driver_state_t *state) { return state->timeout_ms; }
static JSValue recovery_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout)
{
    (void)timeout;
    state->cancelled = true;
    return recovery_error(ctx, state, true);
}
static esp32_mquickjs_resource_key_t recovery_resource(const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return &s_wifi_recovery_future_lane;
}
static const esp32_mquickjs_future_driver_t s_action_recovery_driver = {
    .memory_owner = "wireless.future", .capture = recovery_capture, .start = recovery_start, .poll = recovery_poll, .finish = recovery_finish,
    .cancel = recovery_cancel, .destroy = recovery_destroy, .timeout_ms = recovery_timeout,
    .on_timeout = recovery_on_timeout, .resource_key = recovery_resource,
};
static const esp32_mquickjs_future_driver_t s_raw_tx_recovery_driver = {
    .memory_owner = "wireless.future", .capture = raw_tx_recovery_capture, .start = recovery_start, .poll = recovery_poll, .finish = recovery_finish,
    .cancel = recovery_cancel, .destroy = recovery_destroy, .timeout_ms = recovery_timeout,
    .on_timeout = recovery_on_timeout, .resource_key = recovery_resource,
};
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
static const esp32_mquickjs_future_driver_t s_ftm_recovery_driver = {
    .memory_owner = "wireless.future", .capture = ftm_recovery_capture, .start = recovery_start, .poll = recovery_poll, .finish = recovery_finish,
    .cancel = recovery_cancel, .destroy = recovery_destroy, .timeout_ms = recovery_timeout,
    .on_timeout = recovery_on_timeout, .resource_key = recovery_resource,
};
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static const esp32_mquickjs_future_driver_t s_twt_recovery_driver = {
    .memory_owner = "wireless.future", .capture = twt_recovery_capture, .start = recovery_start, .poll = recovery_poll, .finish = recovery_finish,
    .cancel = recovery_cancel, .destroy = recovery_destroy, .timeout_ms = recovery_timeout,
    .on_timeout = recovery_on_timeout, .resource_key = recovery_resource,
};
#endif
static JSValue recovery_call(JSContext *ctx, JSValue *self, int argc, JSValue *argv, const char *name)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, name);
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "recover");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *module, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return result;
}
static bool recovery_register(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, const char *name,
    const esp32_mquickjs_future_driver_t *driver)
{
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, name);
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "recover");
    bool ok = !JS_IsException(*method) && esp32_mquickjs_future_register_driver(ctx, runtime, *method, driver);
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register %s recovery runtime", name);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
JSValue js_wifi_twt_recover(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    return recovery_call(ctx, self, argc, argv, "twt");
}
bool esp32_mquickjs_init_wifi_twt_recovery_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    return recovery_register(ctx, runtime, "twt", &s_twt_recovery_driver);
}
#endif
JSValue js_wifi_raw_tx_recover(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    return recovery_call(ctx, self, argc, argv, "rawTx");
}
bool esp32_mquickjs_init_wifi_raw_tx_recovery_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    return recovery_register(ctx, runtime, "rawTx", &s_raw_tx_recovery_driver);
}
JSValue js_wifi_action_recover(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    return recovery_call(ctx, self, argc, argv, "action");
}
bool esp32_mquickjs_init_wifi_action_recovery_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    return recovery_register(ctx, runtime, "action", &s_action_recovery_driver);
}
#if CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
JSValue js_wifi_ftm_recover(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    return recovery_call(ctx, self, argc, argv, "ftm");
}
bool esp32_mquickjs_init_wifi_ftm_recovery_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    return recovery_register(ctx, runtime, "ftm", &s_ftm_recovery_driver);
}
#endif
#endif
