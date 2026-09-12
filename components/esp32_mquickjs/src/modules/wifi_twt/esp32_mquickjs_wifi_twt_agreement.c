#include "esp32_mquickjs_wifi_twt.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_agreement_radio.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>

typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    union {
        esp32_mquickjs_wifi_twt_individual_radio_state_t cached;
        esp32_mquickjs_wifi_twt_broadcast_radio_state_t broadcast_cached;
    };
    bool broadcast;
} twt_agreement_handle_t;
struct esp32_mquickjs_future_driver_state {
    union {
        esp32_mquickjs_wifi_itwt_options_t options;
        esp32_mquickjs_wifi_btwt_options_t broadcast_options;
    };
    esp32_mquickjs_wifi_twt_token_t token;
    union {
        esp32_mquickjs_wifi_twt_individual_radio_state_t result;
        esp32_mquickjs_wifi_twt_broadcast_radio_state_t broadcast_result;
    };
    esp32_mquickjs_wifi_twt_information_result_t information;
    uint32_t information_identity, duration_ms;
    twt_agreement_handle_t *handle;
    esp_err_t error;
    bool close, suspend, resume, submitted, transferred, broadcast;
    atomic_bool worker_done, cancel_requested;
};
static atomic_bool s_agreement_cleanup_busy;
static atomic_int_fast64_t s_agreement_next_retry;
static const char s_agreement_setup_lane;
static const char s_agreement_information_lane;
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

static bool agreement_request_close(bool broadcast, const esp32_mquickjs_wifi_twt_token_t *token)
{
    return broadcast ? esp32_mquickjs_wifi_radio_twt_broadcast_request_close(token)
        : esp32_mquickjs_wifi_radio_twt_individual_request_close(token);
}
static bool agreement_present(const esp32_mquickjs_wifi_twt_token_t *token, bool broadcast)
{
    esp32_mquickjs_wifi_twt_token_t tokens[ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS];
    unsigned count = broadcast
        ? esp32_mquickjs_wifi_radio_twt_broadcast_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS, false)
        : esp32_mquickjs_wifi_radio_twt_individual_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL, false);
    for (unsigned i = 0; i < count; ++i)
        if (tokens[i].identity == token->identity && tokens[i].generation == token->generation) return true;
    return false;
}
static void agreement_cleanup_worker(void *opaque)
{
    (void)opaque;
    esp32_mquickjs_wifi_twt_token_t tokens[ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS];
    unsigned count = esp32_mquickjs_wifi_radio_twt_individual_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL, true);
    for (unsigned i = 0; i < count; ++i) (void)esp32_mquickjs_wifi_radio_twt_individual_close(&tokens[i]);
    count = esp32_mquickjs_wifi_radio_twt_broadcast_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS, true);
    for (unsigned i = 0; i < count; ++i) (void)esp32_mquickjs_wifi_radio_twt_broadcast_close(&tokens[i]);
    if (esp32_mquickjs_wifi_twt_information_pending()) (void)esp32_mquickjs_wifi_twt_sdk_information_reap(0);
    atomic_store_explicit(&s_agreement_next_retry, esp_timer_get_time() + 100000, memory_order_relaxed);
    atomic_store_explicit(&s_agreement_cleanup_busy, false, memory_order_release);
}
bool esp32_mquickjs_wifi_twt_agreement_service(void)
{
    esp32_mquickjs_wifi_twt_token_t token;
    if (atomic_load_explicit(&s_agreement_cleanup_busy, memory_order_acquire) ||
        esp_timer_get_time() < atomic_load_explicit(&s_agreement_next_retry, memory_order_relaxed) ||
        (!esp32_mquickjs_wifi_radio_twt_individual_tokens(&token, 1, true) &&
            !esp32_mquickjs_wifi_radio_twt_broadcast_tokens(&token, 1, true) &&
            !esp32_mquickjs_wifi_twt_information_pending())) return false;
    if (atomic_exchange_explicit(&s_agreement_cleanup_busy, true, memory_order_acq_rel)) return false;
    if (esp32_mquickjs_submit_background_worker(agreement_cleanup_worker, NULL)) return true;
    atomic_store_explicit(&s_agreement_next_retry, esp_timer_get_time() + 100000, memory_order_relaxed);
    atomic_store_explicit(&s_agreement_cleanup_busy, false, memory_order_release);
    return false;
}
bool esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy(void)
{
    esp32_mquickjs_wifi_twt_token_t tokens[ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS];
    unsigned count = esp32_mquickjs_wifi_radio_twt_individual_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL, false);
    for (unsigned i = 0; i < count; ++i) (void)esp32_mquickjs_wifi_radio_twt_individual_request_close(&tokens[i]);
    unsigned broadcast_count = esp32_mquickjs_wifi_radio_twt_broadcast_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS, false);
    for (unsigned i = 0; i < broadcast_count; ++i) (void)esp32_mquickjs_wifi_radio_twt_broadcast_request_close(&tokens[i]);
    (void)esp32_mquickjs_wifi_twt_agreement_service();
    return count == 0 && broadcast_count == 0 && !esp32_mquickjs_wifi_twt_information_pending() &&
        !atomic_load_explicit(&s_agreement_cleanup_busy, memory_order_acquire);
}
static twt_agreement_handle_t *agreement_receiver(JSContext *ctx, JSValue self)
{
    if (JS_GetClassID(ctx, self) != JS_CLASS_WIFI_TWT_AGREEMENT) {
        JS_ThrowTypeError(ctx, "expected WiFiTwtAgreement"); return NULL;
    }
    twt_agreement_handle_t *handle = JS_GetOpaque(ctx, self);
    if (handle == NULL) JS_ThrowReferenceError(ctx, "invalid WiFiTwtAgreement");
    return handle;
}
void js_wifi_twt_agreement_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    twt_agreement_handle_t *handle = opaque;
    if (handle != NULL) {
        (void)agreement_request_close(handle->broadcast, &handle->token);
        esp32_mquickjs_memory_payload_free(handle);
    }
}
JSValue js_wifi_twt_agreement_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.twt.setupIndividual() or setupBroadcast()");
}
static JSValue agreement_status_value(JSContext *ctx,
    const esp32_mquickjs_wifi_twt_individual_radio_state_t *state, bool closed)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    bool seen = (state->native.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN) != 0U;
    bool ambiguous = (state->native.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS) != 0U;
    bool native_closed = (state->native.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED) != 0U;
    const wifi_itwt_setup_config_t *config = seen ? &state->native.event.config : &state->requested;
    SET(result, "state", JS_NewString(ctx, closed ? "closed" : state->closing ? "closing" :
        state->submit_error || ambiguous || native_closed || (seen && state->native.event.status != 1) ? "failed" : seen ? "active" : "pending"));
    SET(result, "kind", JS_NewString(ctx, "individual"));
    SET(result, "nativeClosed", JS_NewBool(native_closed));
    SET(result, "connectionId", JS_NewUint32(ctx, state->requested.twt_id));
    SET(result, "radioGeneration", JS_NewUint32(ctx, state->token.generation));
    SET(result, "sequence", state->dispatch.identity ? JS_NewUint32(ctx, state->dispatch.identity) : JS_NULL);
    SET(result, "flowId", seen ? JS_NewUint32(ctx, config->flow_id) : JS_NULL);
    SET(result, "requestedFlowId", JS_NewUint32(ctx, state->requested.flow_id));
    SET(result, "nativeStatusId", seen ? JS_NewInt32(ctx, state->native.event.status) : JS_NULL);
    SET(result, "reason", seen ? JS_NewUint32(ctx, state->native.event.reason) : JS_NULL);
    SET(result, "ambiguous", JS_NewBool(ambiguous));
    SET(result, "setupCommandId", JS_NewUint32(ctx, config->setup_cmd));
    SET(result, "trigger", JS_NewBool(config->trigger));
    SET(result, "announced", JS_NewBool(!config->flow_type));
    SET(result, "wakeDurationUnit", JS_NewString(ctx, config->wake_duration_unit ? "1024us" : "256us"));
    SET(result, "minimumWakeDuration", JS_NewUint32(ctx, config->min_wake_dura));
    SET(result, "wakeIntervalMantissa", JS_NewUint32(ctx, config->wake_invl_mant));
    SET(result, "wakeIntervalExponent", JS_NewUint32(ctx, config->wake_invl_expn));
    char timestamp[21];
    snprintf(timestamp, sizeof(timestamp), "%" PRIu64, state->native.event.target_wake_time);
    SET(result, "targetWakeTimeUs", seen ? JS_NewString(ctx, timestamp) : JS_NULL);
    SET(result, "submitError", state->submit_error ? JS_NewInt32(ctx, state->submit_error) : JS_NULL);
    SET(result, "driverError", state->dispatch.driver_error ? JS_NewInt32(ctx, state->dispatch.driver_error) : JS_NULL);
    SET(result, "handoffError", state->dispatch.handoff_error ? JS_NewInt32(ctx, state->dispatch.handoff_error) : JS_NULL);
    SET(result, "observationError", state->native.observation_error ? JS_NewInt32(ctx, state->native.observation_error) : JS_NULL);
    SET(result, "teardownError", state->native.teardown_error ? JS_NewInt32(ctx, state->native.teardown_error) : JS_NULL);
    SET(result, "teardownObservationError", state->native.teardown_observation_error ? JS_NewInt32(ctx, state->native.teardown_observation_error) : JS_NULL);
    SET(result, "teardownStatusId", (state->native.flags & ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN)
        ? JS_NewUint32(ctx, state->native.teardown_status) : JS_NULL);
    SET(result, "cleanupPending", JS_NewBool(!closed && state->closing));
    SET(result, "cleanupError", !closed && state->cleanup_error ? JS_NewInt32(ctx, state->cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", !closed && state->cleanup_stage ? JS_NewString(ctx, state->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static bool broadcast_setup_accepted(const esp32_mquickjs_wifi_twt_broadcast_radio_state_t *state)
{
    const esp32_mquickjs_wifi_btwt_timer_result_t *native = &state->native;
    return native->held && native->complete && native->seen && !native->ambiguous &&
        !native->native_closed && !native->native_error && !native->submit_error &&
        native->event.status == BTWT_SETUP_SUCCESS && native->event.setup_cmd == TWT_ACCEPT &&
        native->event.btwt_id == state->requested.btwt_id && native->event.min_wake_dura &&
        native->event.wake_invl_mant && native->event.wake_invl_expn <= 31;
}
static JSValue broadcast_agreement_status_value(JSContext *ctx,
    const esp32_mquickjs_wifi_twt_broadcast_radio_state_t *state, bool closed)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    const esp32_mquickjs_wifi_btwt_timer_result_t *native = &state->native;
    const wifi_event_sta_btwt_setup_t *event = &native->event;
    bool accepted = broadcast_setup_accepted(state);
    SET(result, "state", JS_NewString(ctx, closed ? "closed" : state->closing ? "closing" :
        state->submit_error || native->native_error || native->ambiguous || native->native_closed ||
        (native->complete && !accepted) ? "failed" : accepted ? "active" : "pending"));
    SET(result, "kind", JS_NewString(ctx, "broadcast"));
    SET(result, "broadcastId", JS_NewUint32(ctx, state->requested.btwt_id));
    SET(result, "radioGeneration", JS_NewUint32(ctx, state->token.generation));
    SET(result, "sequence", state->dispatch.identity ? JS_NewUint32(ctx, state->dispatch.identity) : JS_NULL);
    SET(result, "nativeStatusId", native->seen ? JS_NewInt32(ctx, event->status) : JS_NULL);
    SET(result, "reason", native->seen ? JS_NewUint32(ctx, event->reason) : JS_NULL);
    SET(result, "ambiguous", JS_NewBool(native->ambiguous));
    SET(result, "nativeClosed", JS_NewBool(native->native_closed));
    SET(result, "setupCommandId", JS_NewInt32(ctx, native->seen ? event->setup_cmd : state->requested.setup_cmd));
    SET(result, "trigger", accepted ? JS_NewBool(event->trigger) : JS_NULL);
    SET(result, "announced", accepted ? JS_NewBool(!event->flow_type) : JS_NULL);
    SET(result, "wakeDurationUnit", JS_NULL);
    SET(result, "minimumWakeDuration", accepted ? JS_NewUint32(ctx, event->min_wake_dura) : JS_NULL);
    SET(result, "wakeIntervalMantissa", accepted ? JS_NewUint32(ctx, event->wake_invl_mant) : JS_NULL);
    SET(result, "wakeIntervalExponent", accepted ? JS_NewUint32(ctx, event->wake_invl_expn) : JS_NULL);
    char timestamp[21];
    snprintf(timestamp, sizeof(timestamp), "%" PRIu64, event->target_wake_time);
    SET(result, "targetWakeTimeUs", accepted ? JS_NewString(ctx, timestamp) : JS_NULL);
    SET(result, "submitError", state->submit_error ? JS_NewInt32(ctx, state->submit_error) : JS_NULL);
    SET(result, "driverError", state->dispatch.driver_error ? JS_NewInt32(ctx, state->dispatch.driver_error) : JS_NULL);
    SET(result, "handoffError", state->dispatch.handoff_error ? JS_NewInt32(ctx, state->dispatch.handoff_error) : JS_NULL);
    SET(result, "nativeError", native->native_error ? JS_NewInt32(ctx, native->native_error) : JS_NULL);
    SET(result, "observationError", native->observation_error ? JS_NewInt32(ctx, native->observation_error) : JS_NULL);
    SET(result, "teardownError", state->teardown_error ? JS_NewInt32(ctx, state->teardown_error) : JS_NULL);
    SET(result, "teardownStatusId", state->teardown.completion_seen ? JS_NewUint32(ctx, state->teardown.completion_status) : JS_NULL);
    SET(result, "teardownObservationError", state->teardown.observation_error ? JS_NewInt32(ctx, state->teardown.observation_error) : JS_NULL);
    SET(result, "dialogAttemptsRemaining", JS_NewUint32(ctx, state->dialog_attempts_remaining));
    SET(result, "cleanupPending", JS_NewBool(!closed && state->closing));
    SET(result, "cleanupError", !closed && state->cleanup_error ? JS_NewInt32(ctx, state->cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", !closed && state->cleanup_stage ? JS_NewString(ctx, state->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_twt_agreement_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "WiFiTwtAgreement.status expects no arguments");
    twt_agreement_handle_t *handle = agreement_receiver(ctx, *self);
    if (handle == NULL) return JS_EXCEPTION;
    if (handle->broadcast) {
        esp32_mquickjs_wifi_twt_broadcast_radio_state_t state;
        if (esp32_mquickjs_wifi_radio_twt_broadcast_status(&handle->token, &state)) {
            if (!state.result_released) handle->broadcast_cached = state;
            return broadcast_agreement_status_value(ctx, &state, false);
        }
        if (agreement_present(&handle->token, true)) return JS_ThrowInternalError(ctx, "TWT state changed during snapshot; retry status");
        return broadcast_agreement_status_value(ctx, &handle->broadcast_cached, true);
    }
    esp32_mquickjs_wifi_twt_individual_radio_state_t state;
    if (esp32_mquickjs_wifi_radio_twt_individual_status(&handle->token, &state)) {
        if (!state.result_released) handle->cached = state;
        return agreement_status_value(ctx, &state, false);
    }
    if (agreement_present(&handle->token, false)) return JS_ThrowInternalError(ctx, "TWT state changed during snapshot; retry status");
    return agreement_status_value(ctx, &handle->cached, true);
}
JSValue js_wifi_twt_agreements(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.twt.agreements expects no arguments");
    esp32_mquickjs_wifi_twt_token_t tokens[ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS];
    unsigned count = esp32_mquickjs_wifi_radio_twt_individual_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL, false);
    JSGCRef array_ref, item_ref;
    JSValue *array = JS_PushGCRef(ctx, &array_ref), *item = JS_PushGCRef(ctx, &item_ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    unsigned written = 0;
    for (unsigned i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_twt_individual_radio_state_t state;
        if (!esp32_mquickjs_wifi_radio_twt_individual_status(&tokens[i], &state)) continue;
        *item = agreement_status_value(ctx, &state, false);
        if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *array, written++, *item))) goto fail;
    }
    count = esp32_mquickjs_wifi_radio_twt_broadcast_tokens(tokens, ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS, false);
    for (unsigned i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_twt_broadcast_radio_state_t state;
        if (!esp32_mquickjs_wifi_radio_twt_broadcast_status(&tokens[i], &state)) continue;
        *item = broadcast_agreement_status_value(ctx, &state, false);
        if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *array, written++, *item))) goto fail;
    }
    JS_PopGCRef(ctx, &item_ref); return JS_PopGCRef(ctx, &array_ref);
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &array_ref); return JS_EXCEPTION;
}
static JSValue agreement_error(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, bool timeout)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    esp_err_t error = timeout ? ESP_ERR_TIMEOUT : state->error;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "stage", JS_NewString(ctx, timeout ? "deadline" : state->close ? "close" : state->resume ? "resume" : state->suspend ? "suspend" : "setup"));
    SET(details, "informationSequence", !timeout && state->information_identity ? JS_NewUint32(ctx, state->information_identity) : JS_NULL);
    SET(details, "informationError", !timeout && state->information.native_error ? JS_NewInt32(ctx, state->information.native_error) : JS_NULL);
    /* Timeout may overlap the submit worker: do not read its result/token. */
    bool seen = false;
    int native_status = 0, reason = 0;
    esp_err_t sdk_error = ESP_OK, driver_error = ESP_OK, handoff_error = ESP_OK;
    if (!timeout) {
        if (state->broadcast) {
            seen = state->broadcast_result.native.seen;
            native_status = state->broadcast_result.native.event.status;
            reason = state->broadcast_result.native.event.reason;
            sdk_error = state->broadcast_result.dispatch.sdk_error;
            driver_error = state->broadcast_result.dispatch.driver_error;
            handoff_error = state->broadcast_result.dispatch.handoff_error;
        } else {
            seen = (state->result.native.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN) != 0;
            native_status = state->result.native.event.status;
            reason = state->result.native.event.reason;
            sdk_error = state->result.dispatch.sdk_error;
            driver_error = state->result.dispatch.driver_error;
            handoff_error = state->result.dispatch.handoff_error;
        }
    }
    SET(details, "nativeStatusId", seen ? JS_NewInt32(ctx, native_status) : JS_NULL);
    SET(details, "reason", seen ? JS_NewInt32(ctx, reason) : JS_NULL);
    SET(details, "sdkError", sdk_error ? JS_NewInt32(ctx, sdk_error) : JS_NULL);
    SET(details, "driverError", driver_error ? JS_NewInt32(ctx, driver_error) : JS_NULL);
    SET(details, "handoffError", handoff_error ? JS_NewInt32(ctx, handoff_error) : JS_NULL);
    (void)esp32_mquickjs_throw_native_error(ctx, timeout ? "WIFI_TWT_AGREEMENT_TIMEOUT" : "WIFI_TWT_AGREEMENT_FAILED",
        state->close ? "WiFiTwtAgreement.close" : state->resume ? "WiFiTwtAgreement.resume" : state->suspend ? "WiFiTwtAgreement.suspend" : state->broadcast ? "wifi.twt.setupBroadcast" : "wifi.twt.setupIndividual",
        "TWT agreement operation failed; inspect wifi.twt.status() and agreements() for retained state", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static bool agreement_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, unsigned operation)
{
    *output = NULL;
    bool close = operation == 1, resume = operation == 3, suspend = operation == 2 || resume;
    bool broadcast = operation == 4;
    esp32_mquickjs_wifi_btwt_options_t broadcast_options = {0};
    uint32_t duration_ms = 0;
    esp32_mquickjs_wifi_itwt_options_t options = {.timeout_ms = ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS};
    twt_agreement_handle_t *handle = NULL;
    if (close || suspend) {
        handle = agreement_receiver(ctx, self->val);
        if (handle == NULL) return false;
        broadcast = handle->broadcast;
        if (broadcast && suspend) {
            JS_ThrowTypeError(ctx, "suspend/resume require an individual TWT agreement"); return false;
        }
        if (argc > 1 || (suspend && !resume && (argc != 1 || JS_IsUndefined(argv[0].val)))) {
            JS_ThrowTypeError(ctx, "suspend expects durationMs options; close/resume accept optional timeoutMs"); return false;
        }
        if (argc == 1 && !JS_IsUndefined(argv[0].val)) {
            static const char *const keys[] = {"timeoutMs", "durationMs"};
            if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val,
                resume ? "WiFiTwtAgreement.resume" : suspend ? "WiFiTwtAgreement.suspend" : "WiFiTwtAgreement.close",
                keys, suspend && !resume ? 2 : 1)) return false;
            JSGCRef ref; JSValue *value = JS_PushGCRef(ctx, &ref);
            *value = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
            bool ok = !JS_IsException(*value) && (JS_IsUndefined(*value) ||
                esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 60000, &options.timeout_ms));
            if (ok && suspend && !resume) {
                *value = JS_GetPropertyStr(ctx, argv[0].val, "durationMs");
                ok = !JS_IsException(*value) && !JS_IsUndefined(*value) &&
                    esp32_mquickjs_value_to_bounded_u32(ctx, *value, 0, ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS, &duration_ms);
            }
            JS_PopGCRef(ctx, &ref);
            if (!ok) { if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid TWT duration or timeout"); return false; }
        }
    } else if (broadcast) {
        if (argc != 1 || !esp32_mquickjs_wifi_capture_btwt(ctx, argv[0].val, &broadcast_options)) {
            if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "setupBroadcast expects options");
            return false;
        }
    } else if (argc != 1 || !esp32_mquickjs_wifi_capture_itwt(ctx, argv[0].val, &options)) {
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "setupIndividual expects options");
        return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); return false; }
    state->broadcast = broadcast;
    if (broadcast) {
        state->broadcast_options = broadcast_options;
        if (close) state->broadcast_options.timeout_ms = options.timeout_ms;
    } else state->options = options;
    state->close = close; state->suspend = suspend; state->resume = resume; state->duration_ms = duration_ms;
    atomic_init(&state->worker_done, false); atomic_init(&state->cancel_requested, false);
    if (close || suspend) {
        state->token = handle->token;
        if (broadcast) state->broadcast_result = handle->broadcast_cached;
        else state->result = handle->cached;
    }
    else {
        state->handle = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*state->handle), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (state->handle == NULL) { esp32_mquickjs_memory_payload_free(state); JS_ThrowOutOfMemory(ctx); return false; }
    }
    *output = state; return true;
}
static bool agreement_open_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output) { return agreement_capture(ctx, self, argc, argv, output, false); }
static bool agreement_broadcast_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output) { return agreement_capture(ctx, self, argc, argv, output, 4); }
static bool agreement_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output) { return agreement_capture(ctx, self, argc, argv, output, true); }
static bool agreement_suspend_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output) { return agreement_capture(ctx, self, argc, argv, output, 2); }
static bool agreement_resume_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output) { return agreement_capture(ctx, self, argc, argv, output, 3); }
static void agreement_submit_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    if (!atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) {
        if (state->resume) state->error = esp32_mquickjs_wifi_radio_twt_individual_resume(&state->token, &state->information_identity);
        else if (state->suspend) state->error = esp32_mquickjs_wifi_radio_twt_individual_suspend(
            &state->token, state->duration_ms, &state->information_identity);
        else if (state->broadcast) state->error = esp32_mquickjs_wifi_radio_twt_broadcast_submit(&state->broadcast_options, &state->token);
        else state->error = esp32_mquickjs_wifi_radio_twt_individual_submit(&state->options, &state->token);
    }
    atomic_store_explicit(&state->worker_done, true, memory_order_release);
}
static void agreement_schedule(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->submitted || atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return;
    if (state->close) {
        (void)agreement_request_close(state->broadcast, &state->token);
        state->submitted = true;
        atomic_store_explicit(&state->worker_done, true, memory_order_release);
    } else state->submitted = esp32_mquickjs_submit_background_worker(agreement_submit_worker, state);
}
static bool agreement_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token; agreement_schedule(state); return true;
}
static esp32_mquickjs_future_poll_t agreement_poll(esp32_mquickjs_future_driver_state_t *state)
{
    agreement_schedule(state);
    (void)esp32_mquickjs_wifi_twt_agreement_service();
    if (state->submitted && !atomic_load_explicit(&state->worker_done, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_READY;
    if (!state->submitted) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (state->suspend) {
        bool present = esp32_mquickjs_wifi_twt_information_read(state->information_identity, &state->information);
        if (state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
        if (!present || state->information.abandoned) {
            state->error = ESP_ERR_INVALID_STATE; return ESP32_MQUICKJS_FUTURE_READY;
        }
        if (state->information.submitting || !state->information.complete) return ESP32_MQUICKJS_FUTURE_PENDING;
        state->error = state->information.submit_error ? state->information.submit_error : state->information.native_error;
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->broadcast) {
        bool present = esp32_mquickjs_wifi_radio_twt_broadcast_status(&state->token, &state->broadcast_result);
        if (state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
        if (!present) {
            if (agreement_present(&state->token, true)) return ESP32_MQUICKJS_FUTURE_PENDING;
            if (!state->close) state->error = ESP_ERR_INVALID_STATE;
            return ESP32_MQUICKJS_FUTURE_READY;
        }
        if (state->close) {
            esp_err_t cleanup = state->broadcast_result.cleanup_error;
            if (cleanup != ESP_OK && cleanup != ESP_ERR_NOT_FINISHED) {
                state->error = cleanup; return ESP32_MQUICKJS_FUTURE_READY;
            }
            return ESP32_MQUICKJS_FUTURE_PENDING;
        }
        const esp32_mquickjs_wifi_btwt_timer_result_t *native = &state->broadcast_result.native;
        if (state->broadcast_result.closing || native->ambiguous || native->native_closed) {
            state->error = ESP_ERR_INVALID_STATE; return ESP32_MQUICKJS_FUTURE_READY;
        }
        if (!native->complete || native->tx_busy || native->publishing) return ESP32_MQUICKJS_FUTURE_PENDING;
        if (!broadcast_setup_accepted(&state->broadcast_result))
            state->error = native->native_error ? native->native_error : native->submit_error ? native->submit_error : ESP_ERR_INVALID_STATE;
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->error != ESP_OK) {
        if (state->token.identity != 0U)
            (void)esp32_mquickjs_wifi_radio_twt_individual_status(&state->token, &state->result);
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (!esp32_mquickjs_wifi_radio_twt_individual_status(&state->token, &state->result)) {
        if (agreement_present(&state->token, false)) return ESP32_MQUICKJS_FUTURE_PENDING;
        if (!state->close) state->error = ESP_ERR_INVALID_STATE;
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->close) {
        if (state->result.cleanup_error != ESP_OK && state->result.cleanup_error != ESP_ERR_NOT_FINISHED) {
            state->error = state->result.cleanup_error; return ESP32_MQUICKJS_FUTURE_READY;
        }
        return ESP32_MQUICKJS_FUTURE_PENDING;
    }
    if (state->result.closing || (state->result.native.flags &
        (ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS | ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED))) {
        state->error = ESP_ERR_INVALID_STATE; return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (!(state->result.native.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN)) return ESP32_MQUICKJS_FUTURE_PENDING;
    const wifi_event_sta_itwt_setup_t *event = &state->result.native.event;
    if (event->status != 1 || event->config.setup_cmd != TWT_ACCEPT || event->config.reserved ||
        !event->config.min_wake_dura || !event->config.wake_invl_mant) state->error = ESP_ERR_INVALID_STATE;
    return ESP32_MQUICKJS_FUTURE_READY;
}
static JSValue agreement_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return agreement_error(ctx, state, false);
    if (state->close || state->suspend) return JS_UNDEFINED;
    JSValue object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_TWT_AGREEMENT);
    if (JS_IsException(object)) return object;
    state->handle->token = state->token; state->handle->broadcast = state->broadcast;
    if (state->broadcast) state->handle->broadcast_cached = state->broadcast_result;
    else state->handle->cached = state->result;
    JS_SetOpaque(ctx, object, state->handle);
    state->handle = NULL; state->transferred = true;
    return object;
}
static esp32_mquickjs_cancel_result_t agreement_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static void agreement_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    if (state->suspend) esp32_mquickjs_wifi_twt_information_abandon(state->information_identity);
    else if (!state->close && !state->transferred && state->token.identity != 0U)
        (void)agreement_request_close(state->broadcast, &state->token);
    esp32_mquickjs_memory_payload_free(state->handle); esp32_mquickjs_memory_payload_free(state);
    (void)esp32_mquickjs_wifi_twt_agreement_service();
}
static uint32_t agreement_timeout(const esp32_mquickjs_future_driver_state_t *state) { return state->broadcast ? state->broadcast_options.timeout_ms : state->options.timeout_ms; }
static JSValue agreement_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t ms)
{
    (void)ms; atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    return agreement_error(ctx, state, true);
}
static esp32_mquickjs_resource_key_t agreement_resource(const esp32_mquickjs_future_driver_state_t *state)
{ return state->close ? NULL : state->suspend ? &s_agreement_information_lane : &s_agreement_setup_lane; }
#define AGREEMENT_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = agreement_start, .poll = agreement_poll, \
    .finish = agreement_finish, .cancel = agreement_cancel, .destroy = agreement_destroy, \
    .timeout_ms = agreement_timeout, .on_timeout = agreement_on_timeout, .resource_key = agreement_resource }
static const esp32_mquickjs_future_driver_t s_agreement_open_driver = AGREEMENT_DRIVER(agreement_open_capture);
static const esp32_mquickjs_future_driver_t s_agreement_broadcast_driver = AGREEMENT_DRIVER(agreement_broadcast_capture);
static const esp32_mquickjs_future_driver_t s_agreement_close_driver = AGREEMENT_DRIVER(agreement_close_capture);
static const esp32_mquickjs_future_driver_t s_agreement_suspend_driver = AGREEMENT_DRIVER(agreement_suspend_capture);
static const esp32_mquickjs_future_driver_t s_agreement_resume_driver = AGREEMENT_DRIVER(agreement_resume_capture);
static JSValue agreement_call(JSContext *ctx, JSValue *self, int argc, JSValue *argv, const char *name)
{
    JSGCRef receiver_ref, ref;
    JSValue *receiver = JS_PushGCRef(ctx, &receiver_ref), *method = JS_PushGCRef(ctx, &ref);
    *receiver = *self;
    *method = JS_GetPropertyStr(ctx, *receiver, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *receiver, argc, argv);
    JS_PopGCRef(ctx, &ref); JS_PopGCRef(ctx, &receiver_ref); return result;
}
static JSValue agreement_setup_call(JSContext *ctx, JSValue *self, int argc, JSValue *argv, const char *name)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "twt");
    JSValue result = JS_IsException(*module) ? JS_EXCEPTION : agreement_call(ctx, module, argc, argv, name);
    JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref); return result;
}
JSValue js_wifi_twt_setup_individual(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return agreement_setup_call(ctx, self, argc, argv, "setupIndividual"); }
JSValue js_wifi_twt_setup_broadcast(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return agreement_setup_call(ctx, self, argc, argv, "setupBroadcast"); }
JSValue js_wifi_twt_agreement_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return agreement_call(ctx, self, argc, argv, "close"); }
JSValue js_wifi_twt_agreement_suspend(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return agreement_call(ctx, self, argc, argv, "suspend"); }
JSValue js_wifi_twt_agreement_resume(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return agreement_call(ctx, self, argc, argv, "resume"); }
bool esp32_mquickjs_init_wifi_twt_agreement_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, child_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *child = JS_PushGCRef(ctx, &child_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "wifi");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "twt");
    if (JS_IsException(*child)) goto done;
    if (!esp32_mquickjs_init_wifi_twt_close_runtime(ctx, runtime, child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "setupIndividual");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_agreement_open_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "setupBroadcast");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_agreement_broadcast_driver)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiTwtAgreement");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_agreement_close_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "suspend");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_agreement_suspend_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "resume");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_agreement_resume_driver)) goto done;
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register TWT agreement runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &child_ref); JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
