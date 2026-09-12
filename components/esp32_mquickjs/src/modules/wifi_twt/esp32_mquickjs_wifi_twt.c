#include "esp32_mquickjs_wifi_twt.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_radio.h"
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_wifi_twt_information.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_twt_token_t token;
    esp32_mquickjs_wifi_twt_probe_radio_state_t result;
    uint32_t response_ms, timeout_ms;
    esp_err_t error;
    const char *stage;
    bool submitted;
    atomic_bool worker_done, cancel_requested;
};
/* One Future resource lane transfers only a token here. Radio keeps the marker
 * address and native owner; no Future/JS/runtime pointer enters this record. */
static portMUX_TYPE s_twt_retired_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    esp32_mquickjs_wifi_twt_token_t token;
    int64_t next_retry_us;
    esp_err_t error;
    bool active, busy;
} s_twt_retired;
static const char s_twt_future_lane;

static bool twt_retired_pending(void)
{
    portENTER_CRITICAL(&s_twt_retired_lock);
    bool pending = s_twt_retired.active;
    portEXIT_CRITICAL(&s_twt_retired_lock);
    return pending;
}
static void twt_cleanup_worker(void *opaque)
{
    (void)opaque;
    portENTER_CRITICAL(&s_twt_retired_lock);
    esp32_mquickjs_wifi_twt_token_t token = s_twt_retired.token;
    portEXIT_CRITICAL(&s_twt_retired_lock);
    esp32_mquickjs_wifi_twt_token_t current = {0};
    esp_err_t error = ESP_OK;
    if (esp32_mquickjs_wifi_radio_twt_probe_cleanup_token(&current) &&
        current.identity == token.identity && current.generation == token.generation)
        error = esp32_mquickjs_wifi_radio_twt_probe_retire(&token);
    else token = (esp32_mquickjs_wifi_twt_token_t){0}; /* Already retired; never touch a successor. */
    int64_t next = esp_timer_get_time() + 100000;
    portENTER_CRITICAL(&s_twt_retired_lock);
    s_twt_retired.token = token;
    s_twt_retired.active = token.identity != 0U;
    s_twt_retired.error = error;
    s_twt_retired.next_retry_us = next;
    s_twt_retired.busy = false;
    portEXIT_CRITICAL(&s_twt_retired_lock);
}
bool esp32_mquickjs_wifi_twt_service(void)
{
    bool agreement_work = esp32_mquickjs_wifi_twt_agreement_service();
    esp32_mquickjs_wifi_twt_token_t pending = {0};
    if (esp32_mquickjs_wifi_radio_twt_probe_cleanup_token(&pending)) {
        portENTER_CRITICAL(&s_twt_retired_lock);
        if (!s_twt_retired.active && !s_twt_retired.busy) {
            s_twt_retired.token = pending;
            s_twt_retired.active = true;
            s_twt_retired.error = ESP_OK;
            s_twt_retired.next_retry_us = 0;
        }
        portEXIT_CRITICAL(&s_twt_retired_lock);
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_twt_retired_lock);
    bool submit = s_twt_retired.active && !s_twt_retired.busy && now >= s_twt_retired.next_retry_us;
    if (submit) s_twt_retired.busy = true;
    portEXIT_CRITICAL(&s_twt_retired_lock);
    if (!submit) return agreement_work;
    if (esp32_mquickjs_submit_background_worker(twt_cleanup_worker, NULL)) return true;
    portENTER_CRITICAL(&s_twt_retired_lock);
    s_twt_retired.busy = false;
    s_twt_retired.next_retry_us = now + 100000;
    portEXIT_CRITICAL(&s_twt_retired_lock);
    return agreement_work;
}
bool esp32_mquickjs_prepare_wifi_twt_runtime_destroy(void)
{
    bool agreements_closed = esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy();
    (void)esp32_mquickjs_wifi_twt_service();
    return agreements_closed && !twt_retired_pending();
}
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)
static JSValue twt_control_error(JSContext *ctx, const char *operation,
    const esp32_mquickjs_wifi_radio_config_result_t *state)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, state->error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(state->error)));
    SET(details, "stage", state->stage ? JS_NewString(ctx, state->stage) : JS_NULL);
    SET(details, "mutationAttempted", JS_NewBool(state->mutation_attempted));
    JSValue result = esp32_mquickjs_throw_native_error(ctx, "WIFI_TWT_CONTROL_FAILED", operation,
        "TWT control failed; inspect the native error and Radio state", *details);
    JS_PopGCRef(ctx, &ref);
    return result;
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static bool twt_policy_capture(JSContext *ctx, JSValue input, wifi_twt_config_t *config)
{
    static const char *const keys[] = {"postWakeupEvents", "keepAlive"};
    JSGCRef options_ref, value_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *value = JS_PushGCRef(ctx, &value_ref);
    *options = input;
    *config = (wifi_twt_config_t){0};
    bool valid = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, *options, "wifi.twt.configure", keys, 2)) goto done;
    for (unsigned i = 0; i < 2; ++i) {
        *value = JS_GetPropertyStr(ctx, *options, keys[i]);
        if (JS_IsException(*value)) goto done;
        if (!JS_IsBool(*value)) { JS_ThrowTypeError(ctx, "TWT policy requires postWakeupEvents and keepAlive booleans"); goto done; }
        if (i == 0) config->post_wakeup_event = *value == JS_TRUE;
        else config->twt_enable_keep_alive = *value == JS_TRUE;
    }
    valid = true;
done:
    JS_PopGCRef(ctx, &value_ref); JS_PopGCRef(ctx, &options_ref);
    return valid;
}

static JSValue twt_policy_value(JSContext *ctx, const wifi_twt_config_t *config)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "postWakeupEvents", JS_NewBool(config->post_wakeup_event));
    SET(result, "keepAlive", JS_NewBool(config->twt_enable_keep_alive));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_twt_get_config(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.twt.getConfig() expects no arguments");
    esp32_mquickjs_wifi_twt_control_t value = {0};
    esp32_mquickjs_wifi_radio_config_result_t result;
    uint32_t generation;
    esp_err_t error = esp32_mquickjs_wifi_radio_twt_control(NULL, NULL, NULL,
        ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG, &value, &generation, &result);
    if (error != ESP_OK) return twt_control_error(ctx, "wifi.twt.getConfig", &result);
    return twt_policy_value(ctx, &value.config);
}

JSValue js_wifi_twt_configure(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.twt.configure(config) expects one argument");
    esp32_mquickjs_wifi_twt_control_t value = {0};
    if (!twt_policy_capture(ctx, argv[0], &value.config)) return JS_EXCEPTION;
    esp32_mquickjs_wifi_radio_config_result_t result;
    uint32_t generation;
    esp_err_t error = esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,
        &value, &generation, &result);
    if (error != ESP_OK) return twt_control_error(ctx, "wifi.twt.configure", &result);
    return twt_policy_value(ctx, &value.config);
}

JSValue js_wifi_twt_get_flow_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.twt.getFlowStatus() expects no arguments");
    esp32_mquickjs_wifi_twt_control_t value = {0};
    esp32_mquickjs_wifi_radio_config_result_t state;
    uint32_t generation;
    esp_err_t error = esp32_mquickjs_wifi_radio_twt_control(NULL, NULL, NULL,
        ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS, &value, &generation, &state);
    if (error != ESP_OK) return twt_control_error(ctx, "wifi.twt.getFlowStatus", &state);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "radioGeneration", JS_NewUint32(ctx, generation));
    SET(result, "bitmap", JS_NewUint32(ctx, value.flow_bitmap));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_twt_set_target_wake_time_offset(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    esp32_mquickjs_wifi_twt_control_t value = {0};
    if (argc != 1 || !esp32_mquickjs_value_to_bounded_u32(ctx, argv[0], 0, 102400, &value.offset_us)) {
        if (JS_HasException(ctx)) return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, "wifi.twt.setTargetWakeTimeOffset expects integer microseconds 0..102400");
    }
    esp32_mquickjs_wifi_radio_config_result_t result;
    uint32_t generation;
    esp_err_t error = esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,
        &value, &generation, &result);
    if (error != ESP_OK) return twt_control_error(ctx, "wifi.twt.setTargetWakeTimeOffset", &result);
    return JS_NewUint32(ctx, value.offset_us);
}
static const char *twt_probe_status_name(unsigned status)
{
    switch (status) {
    case ITWT_PROBE_SUCCESS: return "success";
    case ITWT_PROBE_FAIL: return "failed";
    case ITWT_PROBE_TIMEOUT: return "timeout";
    case ITWT_PROBE_STA_DISCONNECTED: return "disconnected";
    default: return "unknown";
    }
}
static JSValue twt_error(JSContext *ctx, const char *code, esp_err_t error, const char *stage,
    const esp32_mquickjs_wifi_twt_probe_radio_state_t *state)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "stage", stage ? JS_NewString(ctx, stage) : JS_NULL);
    SET(details, "nativeStatus", state->native.event_seen ? JS_NewString(ctx, twt_probe_status_name(state->native.event.status)) : JS_NULL);
    SET(details, "nativeReason", state->native.event_seen ? JS_NewUint32(ctx, state->native.event.reason) : JS_NULL);
    JSValue result = esp32_mquickjs_throw_native_error(ctx, code, "wifi.twt.probe",
        "TWT probe failed; inspect wifi.twt.status() for native cleanup", *details);
    JS_PopGCRef(ctx, &ref);
    return result;
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static bool twt_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    (void)self;
    *output = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "wifi.twt.probe expects optional options"); return false; }
    uint32_t response = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_RESPONSE_MS, timeout = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS;
    if (argc == 1 && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"responseTimeoutMs", "timeoutMs"};
        if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val, "wifi.twt.probe", keys, 2)) return false;
        JSGCRef ref;
        JSValue *field = JS_PushGCRef(ctx, &ref);
        bool valid = true;
        *field = JS_GetPropertyStr(ctx, argv[0].val, "responseTimeoutMs");
        if (JS_IsException(*field) || (!JS_IsUndefined(*field) &&
            !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS, &response))) valid = false;
        if (valid) {
            *field = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
            if (JS_IsException(*field) || (!JS_IsUndefined(*field) &&
                !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS, &timeout))) valid = false;
        }
        JS_PopGCRef(ctx, &ref);
        if (!valid) {
            if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "TWT timeouts must be integers from 1 to 60000 ms");
            return false;
        }
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); return false; }
    state->response_ms = response; state->timeout_ms = timeout;
    atomic_init(&state->worker_done, false); atomic_init(&state->cancel_requested, false);
    *output = state;
    return true;
}
static void twt_submit_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    if (!atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) {
        state->stage = "submit";
        state->error = esp32_mquickjs_wifi_radio_twt_probe_submit(state->response_ms, &state->token);
        if (state->token.identity != 0U)
            (void)esp32_mquickjs_wifi_radio_twt_probe_status(&state->token, &state->result);
    }
    /* Final worker access; Future core retains storage until this publication. */
    atomic_store_explicit(&state->worker_done, true, memory_order_release);
}
static void twt_schedule(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->submitted || atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return;
    (void)esp32_mquickjs_wifi_twt_service();
    if (!twt_retired_pending()) state->submitted = esp32_mquickjs_submit_background_worker(twt_submit_worker, state);
}
static bool twt_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    twt_schedule(state); return true;
}
static esp32_mquickjs_future_poll_t twt_poll(esp32_mquickjs_future_driver_state_t *state)
{
    twt_schedule(state);
    if (state->submitted && !atomic_load_explicit(&state->worker_done, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_READY;
    if (!state->submitted) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    if (!esp32_mquickjs_wifi_radio_twt_probe_status(&state->token, &state->result)) {
        state->error = ESP_ERR_INVALID_STATE; state->stage = "completion-identity";
    } else if (state->result.cleanup_pending) {
        state->error = ESP_ERR_INVALID_STATE; state->stage = "recovery-cancelled";
    } else if (state->result.native_error != ESP_OK) {
        state->error = state->result.native_error; state->stage = "native-control";
    } else if (state->result.native.cancel_requested && !state->result.native.event_seen) {
        state->error = ESP_ERR_INVALID_STATE; state->stage = "native-cancelled";
    }
    return state->error != ESP_OK || state->result.native.event_seen ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue twt_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return twt_error(ctx, "WIFI_TWT_PROBE_FAILED", state->error, state->stage, &state->result);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, state->result.native_identity));
    SET(result, "radioGeneration", JS_NewUint32(ctx, state->token.generation));
    SET(result, "status", JS_NewString(ctx, twt_probe_status_name(state->result.native.event.status)));
    SET(result, "reason", JS_NewUint32(ctx, state->result.native.event.reason));
    SET(result, "correlation", JS_NewString(ctx, "associated-ap-liveness"));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static esp32_mquickjs_cancel_result_t twt_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static void twt_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    (void)esp32_mquickjs_wifi_radio_twt_probe_request_close(&state->token);
    esp32_mquickjs_memory_payload_free(state);
    (void)esp32_mquickjs_wifi_twt_service();
}
static uint32_t twt_timeout(const esp32_mquickjs_future_driver_state_t *state) { return state->timeout_ms; }
static JSValue twt_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    /* The worker may still be writing result. Do not read it before done. */
    const esp32_mquickjs_wifi_twt_probe_radio_state_t empty = {0};
    return twt_error(ctx, "WIFI_TWT_TIMEOUT", ESP_ERR_TIMEOUT, "deadline", &empty);
}
static esp32_mquickjs_resource_key_t twt_resource(const esp32_mquickjs_future_driver_state_t *state)
{ (void)state; return &s_twt_future_lane; }
static const esp32_mquickjs_future_driver_t s_twt_probe_driver = {
    .memory_owner = "wireless.future", .capture = twt_capture, .start = twt_start, .poll = twt_poll, .finish = twt_finish,
    .cancel = twt_cancel, .destroy = twt_destroy, .timeout_ms = twt_timeout,
    .on_timeout = twt_on_timeout, .resource_key = twt_resource,
};
static JSValue twt_information_value(JSContext *ctx)
{
    esp32_mquickjs_wifi_twt_information_result_t state;
    if (!esp32_mquickjs_wifi_twt_information_snapshot(&state)) return JS_NULL;
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, state.identity));
    SET(result, "agreementSequence", JS_NewUint32(ctx, state.setup_identity));
    SET(result, "operation", JS_NewString(ctx, state.resume ? "resume" : "suspend"));
    SET(result, "flowId", JS_NewUint32(ctx, state.flow));
    SET(result, "durationMs", JS_NewUint32(ctx, state.duration_ms));
    SET(result, "dispatching", JS_NewBool(state.submitting));
    SET(result, "complete", JS_NewBool(state.complete));
    SET(result, "txComplete", JS_NewBool(state.tx_complete));
    SET(result, "resumeComplete", JS_NewBool(state.resume_complete));
    SET(result, "cleanupPending", JS_NewBool(state.abandoned));
    SET(result, "submitError", state.submit_error ? JS_NewInt32(ctx, state.submit_error) : JS_NULL);
    SET(result, "nativeError", state.native_error ? JS_NewInt32(ctx, state.native_error) : JS_NULL);
    SET(result, "observationError", state.observation_error ? JS_NewInt32(ctx, state.observation_error) : JS_NULL);
    SET(result, "cleanupError", state.cleanup_error ? JS_NewInt32(ctx, state.cleanup_error) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_twt_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.twt.status expects no arguments");
    esp32_mquickjs_wifi_twt_probe_radio_state_t native;
    esp32_mquickjs_wifi_radio_twt_probe_snapshot(&native);
    portENTER_CRITICAL(&s_twt_retired_lock);
    bool pending = s_twt_retired.active;
    esp_err_t error = s_twt_retired.error;
    portEXIT_CRITICAL(&s_twt_retired_lock);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "operationActive", JS_NewBool(native.token.identity != 0U));
    SET(result, "sequence", native.native_identity ? JS_NewUint32(ctx, native.native_identity) : JS_NULL);
    SET(result, "radioGeneration", native.token.identity ? JS_NewUint32(ctx, native.token.generation) : JS_NULL);
    SET(result, "dispatching", JS_NewBool(native.dispatching));
    SET(result, "nativeOwned", JS_NewBool(native.native.owned));
    SET(result, "nativeStatus", native.native.event_seen ? JS_NewString(ctx, twt_probe_status_name(native.native.event.status)) : JS_NULL);
    SET(result, "nativeReason", native.native.event_seen ? JS_NewUint32(ctx, native.native.event.reason) : JS_NULL);
    SET(result, "nativeError", native.native_error ? JS_NewInt32(ctx, native.native_error) : JS_NULL);
    SET(result, "submitError", native.submit_error ? JS_NewInt32(ctx, native.submit_error) : JS_NULL);
    SET(result, "observationError", native.native.observation_error ? JS_NewInt32(ctx, native.native.observation_error) : JS_NULL);
    SET(result, "cancelRequested", JS_NewBool(native.native.cancel_requested));
    SET(result, "cancelComplete", JS_NewBool(native.native.cancel_complete));
    SET(result, "cleanupPending", JS_NewBool(pending || native.cleanup_pending));
    SET(result, "cleanupError", error ? JS_NewInt32(ctx, error) : native.cleanup_error ? JS_NewInt32(ctx, native.cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", native.cleanup_stage ? JS_NewString(ctx, native.cleanup_stage) : JS_NULL);
    SET(result, "information", twt_information_value(ctx));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_twt_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.twt.capabilities expects no arguments");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-twt/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
    SET(result, "probe", JS_TRUE);
    SET(result, "broadcastDiscovery", JS_TRUE);
    SET(result, "maximumBroadcastSchedules", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST));
    SET(result, "maximumProbes", JS_NewInt32(ctx, 1));
    SET(result, "setupBroadcast", JS_TRUE);
    SET(result, "maximumBroadcastAgreements", JS_NewInt32(ctx, 31));
    SET(result, "maximumBroadcastAttemptsPerId", JS_NewInt32(ctx, 255));
    SET(result, "setupIndividual", JS_TRUE);
    SET(result, "closeAll", JS_TRUE);
    SET(result, "recover", JS_TRUE);
    SET(result, "flowStatus", JS_TRUE);
    SET(result, "targetWakeTimeOffset", JS_TRUE);
    SET(result, "configure", JS_TRUE);
    SET(result, "maximumIndividualAgreements", JS_NewInt32(ctx, 8));
    SET(result, "suspendIndividual", JS_TRUE);
    SET(result, "resumeIndividual", JS_TRUE);
    SET(result, "maximumInformationOperations", JS_NewInt32(ctx, 1));
    SET(result, "maximumSuspendDurationMs", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS));
    SET(result, "maximumResponseTimeoutMs", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS));
    SET(result, "maximumTimeoutMs", JS_NewUint32(ctx, ESP32_MQUICKJS_WIFI_TWT_MAX_TIMEOUT_MS));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_twt_probe(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "twt");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "probe");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *module, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return result;
}
bool esp32_mquickjs_init_wifi_twt_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "twt");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "probe");
    bool ok = !JS_IsException(*method) && esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_twt_probe_driver);
    if (ok) ok = esp32_mquickjs_init_wifi_twt_broadcast_runtime(ctx, runtime, module);
    if (ok) ok = esp32_mquickjs_init_wifi_twt_agreement_runtime(ctx, runtime);
    if (ok) ok = esp32_mquickjs_init_wifi_twt_recovery_runtime(ctx, runtime);
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register TWT runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
