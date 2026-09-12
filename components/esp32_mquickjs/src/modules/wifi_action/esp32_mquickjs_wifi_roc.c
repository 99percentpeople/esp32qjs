#include "esp32_mquickjs_wifi_action.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_roc_session.h"
#include "esp32_mquickjs_wifi_action_sdk.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"

typedef enum { ROC_OPEN, ROC_WAIT, ROC_CLOSE } roc_operation_t;
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_roc_session_t *session;
    uint32_t timeout_ms;
    roc_operation_t operation;
    esp_err_t error;
    bool started, cancelled;
};
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

static esp32_mquickjs_wifi_roc_session_t *roc_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_ROC_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiRocSession"); return NULL;
    }
    esp32_mquickjs_wifi_roc_session_t *session = JS_GetOpaque(ctx, value);
    if (!session) JS_ThrowReferenceError(ctx, "invalid WiFiRocSession");
    return session;
}
void js_wifi_roc_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    esp32_mquickjs_wifi_roc_close(opaque);
    esp32_mquickjs_wifi_roc_release(opaque);
}
JSValue js_wifi_roc_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.action.remainOnChannel()");
}

static bool roc_options(JSContext *ctx, JSGCRef *root, wifi_roc_req_t *output, uint32_t *timeout)
{
    static const char *const keys[] = {"interface", "channel", "secondaryChannel", "durationMs", "allowBroadcast", "timeoutMs"};
    static const char *const interfaces[] = {"station", "access-point"};
    static const char *const secondary[] = {"none", "above", "below"};
    wifi_roc_req_t request = {.ifx = WIFI_IF_STA, .type = WIFI_ROC_REQ, .rx_cb = esp32_mquickjs_wifi_action_receive};
    uint32_t deadline = 1000, number;
    size_t choice;
    JSGCRef ref;
    JSValue *field = JS_PushGCRef(ctx, &ref);
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, root->val, "wifi.action.remainOnChannel", keys, 6)) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, root->val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("interface");
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *field, interfaces, 2, &choice)) goto invalid;
        request.ifx = choice == 0 ? WIFI_IF_STA : WIFI_IF_AP;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (choice != 0) goto invalid;
#endif
    }
    FIELD("channel");
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 177, &number)) goto invalid;
    if (number > 14) {
#if CONFIG_SOC_WIFI_SUPPORT_5G
        if (!esp32_mquickjs_wifi_radio_5ghz_channel_bit(number)) goto invalid;
#else
        goto invalid;
#endif
    }
    request.channel = number;
    FIELD("secondaryChannel");
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *field, secondary, 3, &choice)) goto invalid;
        request.sec_channel = choice == 0 ? WIFI_SECOND_CHAN_NONE : choice == 1 ? WIFI_SECOND_CHAN_ABOVE : WIFI_SECOND_CHAN_BELOW;
    }
    FIELD("durationMs");
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 60000, &request.wait_time_ms)) goto invalid;
    FIELD("allowBroadcast");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        request.allow_broadcast = *field == JS_TRUE;
    }
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 60000, &deadline)) goto invalid;
    *output = request; *timeout = deadline; ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid ROC options");
done:
    JS_PopGCRef(ctx, &ref); return ok;
#undef FIELD
}

static const char *roc_operation_name(roc_operation_t operation)
{
    return operation == ROC_OPEN ? "wifi.action.remainOnChannel" : operation == ROC_CLOSE ? "WiFiRocSession.close" : "WiFiRocSession.wait";
}
static JSValue roc_error(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, bool timeout)
{
    esp32_mquickjs_wifi_roc_status_t status = {0};
    if (state->session) (void)esp32_mquickjs_wifi_roc_status(state->session, &status);
    esp_err_t error = timeout ? ESP_ERR_TIMEOUT : state->error ? state->error : status.error ? status.error : ESP_ERR_INVALID_STATE;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "stage", JS_NewString(ctx, timeout ? "deadline" : status.stage ? status.stage : "roc-state"));
    SET(details, "cleanupPending", JS_NewBool(status.started && !status.retired));
    SET(details, "cleanupError", status.cleanup_error ? JS_NewInt32(ctx, status.cleanup_error) : JS_NULL);
    SET(details, "cleanupStage", status.cleanup_stage ? JS_NewString(ctx, status.cleanup_stage) : JS_NULL);
    (void)esp32_mquickjs_throw_native_error(ctx, timeout ? "WIFI_ROC_TIMEOUT" : "WIFI_ROC_FAILED",
        roc_operation_name(state->operation), "ROC operation did not complete; native residency may still be active", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue roc_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_roc_status_t *status)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "state", JS_NewString(ctx, status->retired ? "closed" : status->close_requested ? "closing" :
        status->error || status->cleanup_error || status->native.ambiguous ? "faulted" : status->submitted ? "active" : "opening"));
    SET(result, "interface", JS_NewString(ctx, status->request.ifx == WIFI_IF_STA ? "station" : "access-point"));
    SET(result, "channel", JS_NewUint32(ctx, status->request.channel));
    SET(result, "secondaryChannel", JS_NewString(ctx, status->request.sec_channel == WIFI_SECOND_CHAN_NONE ? "none" : status->request.sec_channel == WIFI_SECOND_CHAN_ABOVE ? "above" : "below"));
    SET(result, "durationMs", JS_NewUint32(ctx, status->request.wait_time_ms));
    SET(result, "allowBroadcast", JS_NewBool(status->request.allow_broadcast));
    SET(result, "driverAccepted", JS_NewBool(status->submitted && status->error == ESP_OK));
    SET(result, "closeRequested", JS_NewBool(status->close_requested));
    SET(result, "cleanupPending", JS_NewBool(status->started && !status->retired));
    SET(result, "sequence", status->native.identity ? JS_NewUint32(ctx, status->native.identity) : JS_NULL);
    SET(result, "radioGeneration", status->native.identity ? JS_NewUint32(ctx, status->native.generation) : JS_NULL);
    SET(result, "operationId", status->native.submitted ? JS_NewUint32(ctx, status->native.operation_id) : JS_NULL);
    SET(result, "terminalStatus", status->native.terminal ? JS_NewString(ctx, status->native.terminal_status == WIFI_ROC_DONE ? "completed" : "cancelled") : JS_NULL);
    SET(result, "ambiguous", JS_NewBool(status->native.ambiguous));
    SET(result, "nativeQuiescent", JS_NewBool(status->native.sdk_quiescent));
    SET(result, "nativeTerminated", JS_NewBool(status->native.physical_termination));
    SET(result, "error", status->error ? JS_NewInt32(ctx, status->error) : JS_NULL);
    SET(result, "stage", status->stage ? JS_NewString(ctx, status->stage) : JS_NULL);
    SET(result, "cleanupError", status->cleanup_error ? JS_NewInt32(ctx, status->cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", status->cleanup_stage ? JS_NewString(ctx, status->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_roc_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "WiFiRocSession.status expects no arguments");
    esp32_mquickjs_wifi_roc_session_t *session = roc_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_roc_status_t status;
    (void)esp32_mquickjs_wifi_roc_status(session, &status);
    return roc_status_to_js(ctx, &status);
}

static void roc_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state) return;
    bool service = state->started && state->operation != ROC_WAIT;
    if (state->session) {
        /* OPEN transfers its reference to JS only after successful allocation.
         * Error, OOM, timeout and cancelled opens leave no orphan residency. */
        if (state->operation == ROC_OPEN) esp32_mquickjs_wifi_roc_close(state->session);
        esp32_mquickjs_wifi_roc_release(state->session);
    }
    esp32_mquickjs_memory_payload_free(state);
    if (service) (void)esp32_mquickjs_wifi_roc_service();
}
static bool roc_open_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    (void)self; *output = NULL;
    if (argc != 1) { JS_ThrowTypeError(ctx, "wifi.action.remainOnChannel expects options"); return false; }
    wifi_roc_req_t request;
    uint32_t timeout;
    if (!roc_options(ctx, &argv[0], &request, &timeout)) return false;
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { JS_ThrowOutOfMemory(ctx); return false; }
    state->operation = ROC_OPEN; state->timeout_ms = timeout;
    state->error = esp32_mquickjs_wifi_roc_create(&request, &state->session);
    if (state->error != ESP_OK) { (void)roc_error(ctx, state, false); roc_destroy(state); return false; }
    *output = state; return true;
}
static bool roc_method_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, roc_operation_t operation)
{
    *output = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "%s expects optional options", roc_operation_name(operation)); return false; }
    /* Retain the receiver before any getter can trigger GC or close/release.
     * Options parsing may run JS; no raw opaque pointer outlives its native ref. */
    esp32_mquickjs_wifi_roc_session_t *session = roc_receiver(ctx, self->val);
    if (!session) return false;
    if (!esp32_mquickjs_wifi_roc_retain(session)) { JS_ThrowInternalError(ctx, "ROC reference exhausted"); return false; }
    uint32_t timeout = 1000;
    bool valid = true;
    if (argc > 0 && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        valid = esp32_mquickjs_validate_plain_options(ctx, argv[0].val, roc_operation_name(operation), keys, 1);
        if (valid) {
            JSGCRef ref;
            JSValue *value = JS_PushGCRef(ctx, &ref);
            *value = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
            valid = !JS_IsException(*value) && (JS_IsUndefined(*value) || esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 60000, &timeout));
            JS_PopGCRef(ctx, &ref);
        }
    }
    if (!valid) {
        esp32_mquickjs_wifi_roc_release(session);
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid ROC timeout options");
        return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { esp32_mquickjs_wifi_roc_release(session); JS_ThrowOutOfMemory(ctx); return false; }
    state->session = session; state->operation = operation; state->timeout_ms = timeout;
    *output = state; return true;
}
static bool roc_wait_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{ return roc_method_capture(ctx, self, argc, argv, output, ROC_WAIT); }
static bool roc_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{ return roc_method_capture(ctx, self, argc, argv, output, ROC_CLOSE); }
static bool roc_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    state->started = true;
    if (state->operation == ROC_OPEN) state->error = esp32_mquickjs_wifi_roc_start(state->session);
    if (state->operation == ROC_CLOSE) esp32_mquickjs_wifi_roc_close(state->session);
    (void)esp32_mquickjs_wifi_roc_service();
    return true;
}
static esp32_mquickjs_future_poll_t roc_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled || state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    (void)esp32_mquickjs_wifi_roc_service();
    esp32_mquickjs_wifi_roc_status_t status;
    (void)esp32_mquickjs_wifi_roc_status(state->session, &status);
    bool ready = state->operation == ROC_OPEN ? status.submitted || status.retired : status.retired;
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue roc_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_roc_status_t status;
    (void)esp32_mquickjs_wifi_roc_status(state->session, &status);
    if (state->error != ESP_OK || (state->operation != ROC_CLOSE && status.error != ESP_OK)) return roc_error(ctx, state, false);
    if (state->operation == ROC_OPEN) {
        JSValue result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_ROC_SESSION);
        if (JS_IsException(result)) return result;
        JS_SetOpaque(ctx, result, state->session);
        state->session = NULL; /* Transfer the Future-owned native reference. */
        return result;
    }
    return state->operation == ROC_CLOSE ? JS_UNDEFINED : roc_status_to_js(ctx, &status);
}
static esp32_mquickjs_cancel_result_t roc_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    /* wait cancellation never alters the residency. A started close remains
     * irreversible intent; only native drain completes that request. */
    if (state->started && state->operation != ROC_WAIT) esp32_mquickjs_wifi_roc_close(state->session);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static uint32_t roc_timeout(const esp32_mquickjs_future_driver_state_t *state) { return state->timeout_ms; }
static JSValue roc_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    (void)roc_cancel(state);
    return roc_error(ctx, state, true);
}
/* Wait/close have independent Future slots, so a long wait cannot block close.
 * The native Session worker serializes SDK operations and holds its own ref. */
#define ROC_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = roc_start, .poll = roc_poll, .finish = roc_finish, \
    .cancel = roc_cancel, .destroy = roc_destroy, .timeout_ms = roc_timeout, .on_timeout = roc_on_timeout }
static const esp32_mquickjs_future_driver_t s_roc_open_driver = ROC_DRIVER(roc_open_capture);
static const esp32_mquickjs_future_driver_t s_roc_wait_driver = ROC_DRIVER(roc_wait_capture);
static const esp32_mquickjs_future_driver_t s_roc_close_driver = ROC_DRIVER(roc_close_capture);

static JSValue roc_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver;
    *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref);
    return result;
}
JSValue js_wifi_roc_wait(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return roc_call(ctx, self, argc, argv, "wait"); }
JSValue js_wifi_roc_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return roc_call(ctx, self, argc, argv, "close"); }
JSValue js_wifi_action_remain_on_channel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "action");
    JSValue result = JS_IsException(*module) ? JS_EXCEPTION : roc_call(ctx, module, argc, argv, "remainOnChannel");
    JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return result;
}
bool esp32_mquickjs_init_wifi_roc_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, method_ref, child_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref), *child = JS_PushGCRef(ctx, &child_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "wifi");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "action");
    if (JS_IsException(*child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "remainOnChannel");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_roc_open_driver)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiRocSession");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "wait");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_roc_wait_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_roc_close_driver)) goto done;
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register ROC runtime");
    JS_PopGCRef(ctx, &child_ref); JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
