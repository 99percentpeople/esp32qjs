#include "esp32_mquickjs_wifi_raw_tx.h"
#include "esp32_mquickjs_js_macros.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_raw_tx_periodic_job.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_memory.h"
#include "esp_heap_caps.h"

#define job_api(name) esp32_mquickjs_wifi_raw_tx_periodic_job_##name
typedef esp32_mquickjs_wifi_raw_tx_periodic_job_t job_t;
typedef esp32_mquickjs_wifi_raw_tx_periodic_job_status_t status_t;
typedef struct {
    /* JS-task references, independent of native timer/worker references. */
    uint32_t references;
    job_t *job;
    status_t cached;
    bool close_requested;
} periodic_handle_t;
struct esp32_mquickjs_future_driver_state {
    bool closing, started, cancelled;
    uint32_t timeout_ms;
    periodic_handle_t *handle;
    job_t *job;
    esp32_mquickjs_wifi_raw_tx_session_t *session;
    esp32_mquickjs_wifi_raw_tx_session_options_t session_options;
    esp32_mquickjs_wifi_raw_tx_periodic_options_t options;
    uint8_t *bytes;
    size_t length;
    esp_err_t error;
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
};

static periodic_handle_t *periodic_handle(JSContext *ctx, JSValue receiver)
{
    if (JS_GetClassID(ctx, receiver) != JS_CLASS_WIFI_RAW_PERIODIC_TX) {
        JS_ThrowTypeError(ctx, "expected WiFiRawPeriodicTx"); return NULL;
    }
    periodic_handle_t *handle = JS_GetOpaque(ctx, receiver);
    if (handle == NULL) JS_ThrowReferenceError(ctx, "invalid WiFiRawPeriodicTx");
    return handle;
}
static bool handle_retain(periodic_handle_t *handle)
{
    if (handle->references == 0U || handle->references == UINT32_MAX) return false;
    ++handle->references; return true;
}
static void handle_release(periodic_handle_t *handle)
{
    if (handle == NULL || --handle->references != 0U) return;
    if (handle->job != NULL) { job_api(close)(handle->job); job_api(release)(handle->job); }
    esp32_mquickjs_memory_payload_free(handle);
}
static bool handle_snapshot(periodic_handle_t *handle, status_t *output)
{
    if (handle->job != NULL) {
        if (!job_api(status)(handle->job, output)) return false;
        if (output->retired) {
            job_t *job = handle->job;
            handle->cached = *output; handle->job = NULL;
            job_api(release)(job);
        }
    } else *output = handle->cached;
    output->close_requested |= handle->close_requested;
    return true;
}
void js_wifi_raw_periodic_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    periodic_handle_t *handle = opaque;
    if (handle != NULL && handle->job != NULL) job_api(close)(handle->job);
    handle_release(handle);
}
JSValue js_wifi_raw_periodic_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use WiFiRawTxSession.startPeriodic()");
}

static bool periodic_options(JSContext *ctx, JSGCRef *input,
    esp32_mquickjs_wifi_raw_tx_periodic_options_t *output, uint32_t *timeout,
    uint8_t **bytes, size_t *length)
{
    static const char *const keys[] = {"frame", "intervalUs", "count", "startDelayUs", "busyPolicy", "stopOnError", "timeoutMs"};
    static const char *const busy[] = {"skip", "stop"};
    esp32_mquickjs_wifi_raw_tx_periodic_options_t options = {.stop_on_error = true};
    uint32_t deadline = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_TIMEOUT_MS;
    JSGCRef field_ref;
    JSValue *field = JS_PushGCRef(ctx, &field_ref);
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, input->val, "WiFiRawTxSession.startPeriodic", keys, 7)) goto done;
    *field = JS_GetPropertyStr(ctx, input->val, "intervalUs");
    if (JS_IsException(*field)) goto done;
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field,
        ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_MIN_INTERVAL_US, UINT32_MAX, &options.interval_us)) goto invalid;
#define OPTIONAL_U32(key, minimum, maximum, target) \
    *field = JS_GetPropertyStr(ctx, input->val, key); \
    if (JS_IsException(*field)) goto done; \
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, minimum, maximum, target)) goto invalid
    OPTIONAL_U32("count", 0, UINT32_MAX, &options.count);
    OPTIONAL_U32("startDelayUs", 0, UINT32_MAX, &options.start_delay_us);
    OPTIONAL_U32("timeoutMs", 1, INT32_MAX, &deadline);
#undef OPTIONAL_U32
    *field = JS_GetPropertyStr(ctx, input->val, "busyPolicy");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        size_t choice;
        if (!esp32_mquickjs_value_to_enum(ctx, *field, busy, 2, &choice)) goto invalid;
        options.busy = choice;
    }
    *field = JS_GetPropertyStr(ctx, input->val, "stopOnError");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        options.stop_on_error = *field == JS_TRUE;
    }
    /* Read/copy the frame only after all policy getters. No JS pointer or read
     * lease survives capture, and a getter-closed ByteView fails normally. */
    *field = JS_GetPropertyStr(ctx, input->val, "frame");
    if (JS_IsException(*field) || !esp32_mquickjs_wifi_raw_tx_capture_bytes(ctx, &field_ref,
        "WiFiRawTxSession.startPeriodic", bytes, length)) goto done;
    *output = options; *timeout = deadline; ok = true; goto done;
invalid:
    JS_ThrowTypeError(ctx, "invalid Raw TX periodic options");
done:
    JS_PopGCRef(ctx, &field_ref); return ok;
}

static JSValue periodic_error(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, bool timeout)
{
    status_t status = {0};
    if (state->job != NULL) (void)job_api(status)(state->job, &status);
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx); if (JS_IsException(*details)) goto fail;
    esp_err_t error = timeout ? ESP_ERR_TIMEOUT : state->error != ESP_OK ? state->error :
        status.error != ESP_OK ? status.error : ESP_ERR_INVALID_STATE;
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "espCode", JS_NewInt32(ctx, error), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(error)), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "validationCode", JS_NewUint32(ctx, state->validation), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "stage", timeout ? JS_NewString(ctx, "deadline") : status.stage ? JS_NewString(ctx, status.stage) : JS_NewString(ctx, "periodic-state"), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "periodicGeneration", status.ledger.generation ? JS_NewUint32(ctx, status.ledger.generation) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "scheduled", JS_NewUint32(ctx, status.ledger.scheduled), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "issued", JS_NewUint32(ctx, status.ledger.issued), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "submitted", JS_NewUint32(ctx, status.ledger.submitted), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "cleanupPending", JS_NewBool(state->job != NULL && !status.retired), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "cleanupError", status.cleanup_error ? JS_NewInt32(ctx, status.cleanup_error) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "cleanupStage", status.cleanup_stage ? JS_NewString(ctx, status.cleanup_stage) : JS_NULL, fail);
    (void)esp32_mquickjs_throw_native_error(ctx, timeout ? "WIFI_RAW_TX_TIMEOUT" :
        state->validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID ? "WIFI_RAW_TX_INVALID_FRAME" : "WIFI_RAW_TX_PERIODIC_FAILED",
        state->closing ? "WiFiRawPeriodicTx.close" : "WiFiRawTxSession.startPeriodic",
        "Periodic operation did not complete; admitted packets may still transmit", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static void periodic_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    if (state->job != NULL) {
        if (!state->closing) job_api(close)(state->job);
        job_api(release)(state->job);
    }
    handle_release(state->handle);
    esp32_mquickjs_wifi_raw_tx_session_release(state->session);
    esp32_mquickjs_memory_payload_free(state->bytes);
    esp32_mquickjs_memory_payload_free(state);
}
static bool periodic_capture(JSContext *ctx, JSGCRef *receiver, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, bool closing)
{
    *output = NULL;
    if (argc != (closing ? 0 : 1)) { JS_ThrowTypeError(ctx, "invalid periodic argument count"); return false; }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); return false; }
    state->closing = closing; state->timeout_ms = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_TIMEOUT_MS;
    if (closing) {
        periodic_handle_t *handle = periodic_handle(ctx, receiver->val);
        if (handle == NULL) goto fail;
        if (!handle_retain(handle)) { JS_ThrowOutOfMemory(ctx); goto fail; }
        state->handle = handle;
        if (handle->job != NULL) {
            if (!job_api(retain)(handle->job)) { JS_ThrowOutOfMemory(ctx); goto fail; }
            state->job = handle->job;
        }
    } else {
        if (!esp32_mquickjs_wifi_raw_tx_session_capture_owner(ctx, receiver->val, &state->session, &state->session_options)) goto fail;
        if (!periodic_options(ctx, &argv[0], &state->options, &state->timeout_ms, &state->bytes, &state->length)) goto fail;
        esp32_mquickjs_wifi_raw_tx_validation_policy_t policy = {.interface = state->session_options.interface,
            .driver_sequence = state->session_options.driver_sequence};
        esp32_mquickjs_wifi_raw_tx_validated_frame_t frame;
        state->validation = esp32_mquickjs_wifi_raw_tx_validate(state->bytes, state->length, &policy, &frame);
        if (state->validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID) {
            state->error = ESP_ERR_INVALID_ARG;
            (void)periodic_error(ctx, state, false); goto fail;
        }
        state->handle = esp32_mquickjs_memory_wireless_calloc("wifi.raw-tx", 1, sizeof(*state->handle), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (state->handle == NULL) { JS_ThrowOutOfMemory(ctx); goto fail; }
        state->handle->references = 1;
    }
    *output = state; return true;
fail:
    periodic_destroy(state); return false;
}
static bool periodic_start_capture(JSContext *ctx, JSGCRef *receiver, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{ return periodic_capture(ctx, receiver, argc, argv, output, false); }
static bool periodic_close_capture(JSContext *ctx, JSGCRef *receiver, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{ return periodic_capture(ctx, receiver, argc, argv, output, true); }

static void periodic_request_close(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->closing && state->handle != NULL) state->handle->close_requested = true;
    if (state->job != NULL) job_api(close)(state->job);
}
static bool periodic_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    if (state->cancelled) return true;
    state->started = true;
    if (state->closing) periodic_request_close(state);
    else {
        esp32_mquickjs_wifi_raw_tx_payload_t frame = {state->bytes, (uint16_t)state->length};
        state->error = job_api(new)(state->session, &frame, &state->options, &state->job);
        if (state->error == ESP_OK) { state->bytes = NULL; state->length = 0; }
    }
    (void)esp32_mquickjs_wifi_raw_tx_periodic_jobs_service();
    return true;
}
static esp32_mquickjs_future_poll_t periodic_poll(esp32_mquickjs_future_driver_state_t *state)
{
    (void)esp32_mquickjs_wifi_raw_tx_periodic_jobs_service();
    (void)esp32_mquickjs_wifi_raw_tx_sessions_service();
    if (state->cancelled || state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    if (!state->started) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (state->job == NULL) return ESP32_MQUICKJS_FUTURE_READY;
    status_t status;
    if (!job_api(status)(state->job, &status)) { state->error = ESP_ERR_INVALID_STATE; return ESP32_MQUICKJS_FUTURE_READY; }
    bool ready = state->closing ? status.retired : status.ready || status.ledger.faulted || status.retired;
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue periodic_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return periodic_error(ctx, state, false);
    status_t status = {0};
    if (state->closing) {
        if (!handle_snapshot(state->handle, &status) || !status.retired) return periodic_error(ctx, state, false);
        return JS_UNDEFINED;
    }
    if (!job_api(status)(state->job, &status) || !status.ready || status.ledger.faulted || status.close_requested)
        return periodic_error(ctx, state, false);
    JSValue object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_RAW_PERIODIC_TX);
    if (JS_IsException(object)) return JS_EXCEPTION;
    state->handle->job = state->job; state->job = NULL;
    JS_SetOpaque(ctx, object, state->handle); state->handle = NULL;
    return object;
}
static esp32_mquickjs_cancel_result_t periodic_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    if (!state->closing) periodic_request_close(state);
    return ESP32_MQUICKJS_CANCELLED;
}
static uint32_t periodic_timeout(const esp32_mquickjs_future_driver_state_t *state)
{ return state->timeout_ms; }
static void periodic_expire(esp32_mquickjs_future_driver_state_t *state)
{
    periodic_request_close(state); /* A close deadline preserves intent even before start. */
    (void)periodic_cancel(state);
}
static JSValue periodic_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout)
{
    (void)timeout;
    periodic_expire(state);
    return periodic_error(ctx, state, true);
}
#define DRIVER(capture_name) { .memory_owner = "wireless.future", .capture = capture_name, .start = periodic_start, .poll = periodic_poll, .finish = periodic_finish, \
    .cancel = periodic_cancel, .destroy = periodic_destroy, .timeout_ms = periodic_timeout, .on_timeout = periodic_on_timeout }
static const esp32_mquickjs_future_driver_t s_start_driver = DRIVER(periodic_start_capture);
static const esp32_mquickjs_future_driver_t s_close_driver = DRIVER(periodic_close_capture);

static JSValue periodic_wait_call(JSContext *ctx, JSValue *self, int argc, JSValue *argv, bool closing)
{
    JSGCRef self_ref, owner_ref, method_ref;
    JSValue *receiver = JS_PushGCRef(ctx, &self_ref), *owner = JS_PushGCRef(ctx, &owner_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    *receiver = self ? *self : JS_UNDEFINED;
    *owner = JS_GetGlobalObject(ctx);
    *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, closing ? "WiFiRawPeriodicTx" : "WiFiRawTxSession");
    *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "prototype");
    *method = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, closing ? "close" : "startPeriodic");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *receiver, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &owner_ref); JS_PopGCRef(ctx, &self_ref);
    return result;
}
JSValue js_wifi_raw_tx_start_periodic(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return periodic_wait_call(ctx, self, argc, argv, false); }
JSValue js_wifi_raw_periodic_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return periodic_wait_call(ctx, self, argc, argv, true); }
JSValue js_wifi_raw_periodic_stop(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || self == NULL) return JS_ThrowTypeError(ctx, "WiFiRawPeriodicTx.stop expects no arguments");
    periodic_handle_t *handle = periodic_handle(ctx, *self);
    if (handle == NULL) return JS_EXCEPTION;
    if (handle->job != NULL) job_api(stop)(handle->job);
    else { handle->cached.stop_requested = true; handle->cached.ledger.running = false; }
    return JS_UNDEFINED;
}
static JSValue periodic_status_to_js(JSContext *ctx, const status_t *snapshot)
{
    const status_t status = *snapshot;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    const char *state = status.retired && status.close_requested ? "closed" : status.ledger.faulted ? "faulted" :
        status.close_requested ? "closing" : status.ledger.running ? "running" : "stopped";
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "state", JS_NewString(ctx, state), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "periodicGeneration", JS_NewUint32(ctx, status.ledger.generation), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "intervalUs", JS_NewUint32(ctx, status.ledger.options.interval_us), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "count", JS_NewUint32(ctx, status.ledger.options.count), fail);
#define TOTAL(js, native) ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, js, JS_NewUint32(ctx, status.ledger.native), fail)
    TOTAL("scheduled", scheduled); TOTAL("issued", issued); TOTAL("submitted", submitted); TOTAL("completed", completed);
    TOTAL("failed", failed); TOTAL("unknown", unknown); TOTAL("rejected", rejected); TOTAL("aborted", aborted);
    TOTAL("dropped", dropped); TOTAL("skippedBusy", skipped_busy); TOTAL("skippedLate", skipped_late);
#undef TOTAL
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "activeSequence", status.ledger.active.sequence ? JS_NewUint32(ctx, status.ledger.active.sequence) : JS_NULL, fail);
#define FLAG(js, value) ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, js, JS_NewBool(value), fail)
    FLAG("retired", status.retired); FLAG("stopRequested", status.stop_requested); FLAG("closeRequested", status.close_requested);
    FLAG("workerBusy", status.worker_busy); FLAG("timerPresent", status.timer_present);
    FLAG("timerQuiesced", status.timer_quiesced); FLAG("timerTransition", status.timer_transition);
    FLAG("faulted", status.ledger.faulted); FLAG("exhausted", status.ledger.exhausted); FLAG("uncertain", status.ledger.uncertain);
    FLAG("cleanupPending", !status.retired && !status.ledger.running);
#undef FLAG
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "error", status.error ? JS_NewInt32(ctx, status.error) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "stage", status.stage ? JS_NewString(ctx, status.stage) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "cleanupError", status.cleanup_error ? JS_NewInt32(ctx, status.cleanup_error) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "cleanupStage", status.cleanup_stage ? JS_NewString(ctx, status.cleanup_stage) : JS_NULL, fail);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_raw_periodic_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || self == NULL) return JS_ThrowTypeError(ctx, "WiFiRawPeriodicTx.status expects no arguments");
    periodic_handle_t *handle = periodic_handle(ctx, *self);
    status_t status;
    if (handle == NULL) return JS_EXCEPTION;
    if (!handle_snapshot(handle, &status)) return JS_ThrowInternalError(ctx, "periodic status unavailable");
    return periodic_status_to_js(ctx, &status);
}

bool esp32_mquickjs_init_wifi_raw_tx_periodic_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef owner_ref, method_ref;
    JSValue *owner = JS_PushGCRef(ctx, &owner_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    const char *const classes[] = {"WiFiRawTxSession", "WiFiRawPeriodicTx"};
    const char *const methods[] = {"startPeriodic", "close"};
    const esp32_mquickjs_future_driver_t *const drivers[] = {&s_start_driver, &s_close_driver};
    for (unsigned i = 0; i < 2; ++i) {
        *owner = JS_GetGlobalObject(ctx);
        *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, classes[i]);
        *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "prototype");
        *method = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, methods[i]);
        if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, drivers[i])) goto done;
    }
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register periodic Future drivers");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &owner_ref); return ok;
}
#endif
