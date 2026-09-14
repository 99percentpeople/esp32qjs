#include "esp32_mquickjs_wifi_raw_tx.h"
#include "esp32_mquickjs_js_macros.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_raw_tx_session.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_native_status.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_memory.h"
#include "esp_heap_caps.h"
#include <string.h>

typedef esp32_mquickjs_wifi_raw_tx_session_t native_session_t;
typedef esp32_mquickjs_wifi_raw_tx_session_status_t native_status_t;
typedef esp32_mquickjs_wifi_raw_tx_session_options_t session_options_t;
typedef esp32_mquickjs_wifi_raw_tx_payload_t payload_t;
typedef struct {
    /* JS task only; a close Future may outlive the JS finalizer. */
    uint32_t references;
    native_session_t *native;
    session_options_t options;
    native_status_t closed_status;
} session_handle_t;
typedef enum { SESSION_OPEN, SESSION_SEND, SESSION_FLUSH, SESSION_CLOSE, SESSION_WRITABLE } session_operation_t;
struct esp32_mquickjs_future_driver_state {
    session_operation_t operation;
    session_handle_t *handle;
    native_session_t *session;
    session_options_t options;
    uint32_t timeout_ms, minimum_packets, minimum_bytes;
    uint8_t *bytes;
    size_t length;
    esp_err_t error;
    const char *stage;
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    esp32_mquickjs_wifi_raw_tx_queue_result_t queue_result;
    bool started, cancelled, admitted;
    union {
        struct {
            esp32_mquickjs_wifi_raw_tx_result_record_t record;
            esp32_mquickjs_wifi_raw_tx_result_token_t token;
        } send;
        esp32_mquickjs_wifi_raw_tx_flush_token_t flush;
    } wait;
};

static const char *session_operation_name(session_operation_t operation)
{
    static const char *const names[] = {"wifi.rawTx.open", "WiFiRawTxSession.send", "WiFiRawTxSession.flush", "WiFiRawTxSession.close", "WiFiRawTxSession.waitWritable"};
    return names[operation];
}

static JSValue session_error(JSContext *ctx, const char *operation, const char *code,
    esp_err_t error, const char *stage, bool admitted, esp32_mquickjs_wifi_raw_tx_validation_t validation,
    esp32_mquickjs_wifi_raw_tx_queue_result_t queue_result)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "native",
        esp32_mquickjs_native_code_to_js(ctx, "esp_err_t", error, esp_err_to_name(error)), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "stage", stage ? JS_NewString(ctx, stage) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "admitted", JS_NewBool(admitted), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "validationCode", JS_NewUint32(ctx, validation), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, details, "queueCode", JS_NewUint32(ctx, queue_result), fail);
    (void)esp32_mquickjs_throw_native_error(ctx, code, operation,
        "Raw TX Session operation did not complete; admitted packets may still transmit", *details);
fail:
    JS_PopGCRef(ctx, &ref);
    return JS_EXCEPTION;
}

static session_handle_t *session_handle(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_RAW_TX_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiRawTxSession"); return NULL;
    }
    session_handle_t *handle = JS_GetOpaque(ctx, value);
    if (handle == NULL) JS_ThrowReferenceError(ctx, "invalid WiFiRawTxSession");
    return handle;
}

bool esp32_mquickjs_wifi_raw_tx_session_capture_owner(JSContext *ctx, JSValue receiver,
    native_session_t **output, session_options_t *options)
{
    session_handle_t *handle = session_handle(ctx, receiver);
    if (handle == NULL) return false;
    if (handle->native == NULL) { JS_ThrowReferenceError(ctx, "WiFiRawTxSession is closed"); return false; }
    if (!esp32_mquickjs_wifi_raw_tx_session_retain(handle->native)) { JS_ThrowOutOfMemory(ctx); return false; }
    *output = handle->native;
    *options = handle->options;
    return true;
}

static bool handle_retain(session_handle_t *handle)
{
    if (handle == NULL || handle->references == 0U || handle->references == UINT32_MAX) return false;
    ++handle->references;
    return true;
}

static void handle_release(session_handle_t *handle)
{
    if (handle == NULL || --handle->references != 0U) return;
    if (handle->native != NULL) {
        esp32_mquickjs_wifi_raw_tx_session_request_close(handle->native);
        esp32_mquickjs_wifi_raw_tx_session_release(handle->native);
    }
    esp32_mquickjs_memory_payload_free(handle);
}

static bool handle_snapshot(session_handle_t *handle, native_status_t *snapshot)
{
    if (handle->native == NULL) { *snapshot = handle->closed_status; return true; }
    if (!esp32_mquickjs_wifi_raw_tx_session_status(handle->native, snapshot)) return false;
    if (snapshot->closed) {
        /* Preserve status without keeping the old native queue/registry owner.
         * Future result/flush registrations retain their own native references. */
        native_session_t *native = handle->native;
        handle->closed_status = *snapshot;
        handle->native = NULL;
        esp32_mquickjs_wifi_raw_tx_session_release(native);
    }
    return true;
}

void js_wifi_raw_tx_session_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    session_handle_t *handle = opaque;
    if (handle == NULL) return;
    if (handle->native != NULL) esp32_mquickjs_wifi_raw_tx_session_request_close(handle->native);
    handle_release(handle);
}

JSValue js_wifi_raw_tx_session_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.rawTx.open() to create a WiFiRawTxSession");
}

static bool capture_timeout(JSContext *ctx, JSValue value, uint32_t *timeout)
{
    *timeout = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_TIMEOUT_MS;
    if (JS_IsUndefined(value)) return true;
    if (esp32_mquickjs_value_to_bounded_u32(ctx, value, 1, INT32_MAX, timeout)) return true;
    JS_ThrowTypeError(ctx, "timeoutMs must be an integer from 1 to 2147483647");
    return false;
}

static bool capture_send_options(JSContext *ctx, JSValue value, uint32_t *timeout)
{
    static const char *const keys[] = {"timeoutMs"};
    *timeout = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_TIMEOUT_MS;
    if (JS_IsUndefined(value)) return true;
    JSGCRef root_ref, field_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref), *field = JS_PushGCRef(ctx, &field_ref);
    *root = value;
    bool ok = esp32_mquickjs_validate_plain_options(ctx, *root, "WiFiRawTxSession.send", keys, 1);
    if (ok) {
        *field = JS_GetPropertyStr(ctx, *root, "timeoutMs");
        ok = !JS_IsException(*field) && capture_timeout(ctx, *field, timeout);
    }
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &root_ref);
    return ok;
}

static bool capture_open_options(JSContext *ctx, JSValue value, session_options_t *output, uint32_t *timeout)
{
    static const char *const keys[] = {"interface", "channel", "sequenceControl", "queue", "timeoutMs", "rate", "maxInFlight"};
    static const char *const interfaces[] = {"station", "access-point"};
    static const char *const sequences[] = {"driver", "application"};
    static const char *const current[] = {"current"};
    static const char *const queue_keys[] = {"capacityPackets", "overflow", "capacityBytes"};
    static const char *const overflows[] = {"reject-newest", "drop-oldest-batch"};
    session_options_t options = {.driver_sequence = true, .capacity = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_QUEUE_PACKETS};
    *timeout = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_TIMEOUT_MS;
    if (JS_IsUndefined(value)) { *output = options; return true; }
    JSGCRef root_ref, queue_ref, field_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref), *queue = JS_PushGCRef(ctx, &queue_ref);
    JSValue *field = JS_PushGCRef(ctx, &field_ref);
    *root = value;
    bool ok = false;
    uint32_t number;
    size_t choice;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.rawTx.open", keys, 7)) goto done;
    *field = JS_GetPropertyStr(ctx, *root, "interface");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *field, interfaces, 2, &choice)) goto invalid;
        options.interface = choice;
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        if (options.interface == ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT) goto invalid;
#endif
    }
    *field = JS_GetPropertyStr(ctx, *root, "channel");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (JS_IsString(ctx, *field)) {
            if (!esp32_mquickjs_value_to_enum(ctx, *field, current, 1, &choice)) goto invalid;
        } else {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 177, &number)) goto invalid;
            if (number > 14U) {
#if CONFIG_SOC_WIFI_SUPPORT_5G
                if (esp32_mquickjs_wifi_radio_5ghz_channel_bit(number) == 0U) goto invalid;
#else
                goto invalid;
#endif
            }
            options.channel = number;
        }
    }
    *field = JS_GetPropertyStr(ctx, *root, "sequenceControl");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *field, sequences, 2, &choice)) goto invalid;
        options.driver_sequence = choice == 0U;
    }
    *queue = JS_GetPropertyStr(ctx, *root, "queue");
    if (JS_IsException(*queue)) goto done;
    if (!JS_IsUndefined(*queue)) {
        if (!esp32_mquickjs_validate_plain_options(ctx, *queue, "wifi.rawTx.open.queue", queue_keys, 3)) goto done;
        *field = JS_GetPropertyStr(ctx, *queue, "capacityPackets");
        if (JS_IsException(*field)) goto done;
        if (!JS_IsUndefined(*field)) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_PACKETS, &number)) goto invalid;
            options.capacity = number;
        }
        *field = JS_GetPropertyStr(ctx, *queue, "capacityBytes");
        if (JS_IsException(*field)) goto done;
        if (!JS_IsUndefined(*field)) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, ESP32_MQUICKJS_WIFI_RAW_TX_MIN_FRAME_BYTES, (uint32_t)options.capacity * ESP32_MQUICKJS_WIFI_RAW_TX_MAX_FRAME_BYTES, &number)) goto invalid;
            options.capacity_bytes = number;
        }
        *field = JS_GetPropertyStr(ctx, *queue, "overflow");
        if (JS_IsException(*field)) goto done;
        if (!JS_IsUndefined(*field)) {
            if (!esp32_mquickjs_value_to_enum(ctx, *field, overflows, 2, &choice)) goto invalid;
            options.overflow = choice;
        }
    }
    *field = JS_GetPropertyStr(ctx, *root, "maxInFlight");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1,
            options.capacity < ESP32_MQUICKJS_WIFI_RAW_TX_MAX_IN_FLIGHT ? options.capacity : ESP32_MQUICKJS_WIFI_RAW_TX_MAX_IN_FLIGHT, &number)) goto invalid;
        options.max_in_flight = number;
    }
    *field = JS_GetPropertyStr(ctx, *root, "timeoutMs");
    if (JS_IsException(*field) || !capture_timeout(ctx, *field, timeout)) goto done;
    *field = JS_GetPropertyStr(ctx, *root, "rate");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_wifi_tx_rate_capture(ctx, *field, &options.rate)) goto done;
        options.rate_set = true;
    }
    *output = options; ok = true; goto done;
invalid:
    JS_ThrowTypeError(ctx, "invalid wifi.rawTx.open options");
done:
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &root_ref);
    return ok;
}

/* Waiting observes capacity; it never reserves slots or changes queued work. */
static bool capture_writable_options(JSContext *ctx, JSValue value,
    esp32_mquickjs_future_driver_state_t *state)
{
    static const char *const keys[] = {"minimumPackets", "minimumBytes", "timeoutMs"};
    state->minimum_packets = 1;
    state->minimum_bytes = ESP32_MQUICKJS_WIFI_RAW_TX_MIN_FRAME_BYTES;
    if (JS_IsUndefined(value)) return true;
    JSGCRef root_ref, field_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref), *field = JS_PushGCRef(ctx, &field_ref);
    *root = value;
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "WiFiRawTxSession.waitWritable", keys, 3)) goto done;
    *field = JS_GetPropertyStr(ctx, *root, "minimumPackets");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1,
        state->options.capacity, &state->minimum_packets)) goto invalid;
    state->minimum_bytes = state->minimum_packets * ESP32_MQUICKJS_WIFI_RAW_TX_MIN_FRAME_BYTES;
    *field = JS_GetPropertyStr(ctx, *root, "minimumBytes");
    if (JS_IsException(*field)) goto done;
    uint32_t capacity_bytes = state->options.capacity_bytes ? state->options.capacity_bytes : (uint32_t)state->options.capacity * ESP32_MQUICKJS_WIFI_RAW_TX_MAX_FRAME_BYTES;
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0,
        capacity_bytes, &state->minimum_bytes)) goto invalid;
    if (state->minimum_bytes > capacity_bytes) goto invalid;
    *field = JS_GetPropertyStr(ctx, *root, "timeoutMs");
    if (JS_IsException(*field) || !capture_timeout(ctx, *field, &state->timeout_ms)) goto done;
    ok = true; goto done;
invalid:
    JS_ThrowTypeError(ctx, "invalid WiFiRawTxSession.waitWritable capacity");
done:
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &root_ref);
    return ok;
}

static void session_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    if (state->operation == SESSION_SEND && state->wait.send.token.identity != 0U) {
        /* No SDK ever receives this record pointer. Failure must preserve the
         * registered storage rather than free memory still visible to Session. */
        if (!esp32_mquickjs_wifi_raw_tx_session_result_release(state->session, &state->wait.send.token)) return;
    }
    if (state->operation == SESSION_FLUSH && state->wait.flush.identity != 0U) {
        if (!esp32_mquickjs_wifi_raw_tx_session_flush_release(state->session, &state->wait.flush)) return;
    }
    if (state->session != NULL) {
        if (state->operation == SESSION_OPEN) esp32_mquickjs_wifi_raw_tx_session_request_close(state->session);
        esp32_mquickjs_wifi_raw_tx_session_release(state->session);
    }
    handle_release(state->handle);
    esp32_mquickjs_memory_payload_free(state->bytes);
    esp32_mquickjs_memory_payload_free(state);
}

static bool session_capture(JSContext *ctx, JSGCRef *receiver, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, session_operation_t operation)
{
    *output = NULL;
    bool valid_args = operation == SESSION_SEND ? argc >= 1 && argc <= 2 :
        operation == SESSION_CLOSE ? argc == 0 : argc >= 0 && argc <= 1;
    if (!valid_args) { JS_ThrowTypeError(ctx, "invalid Raw TX Session argument count"); return false; }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); return false; }
    state->operation = operation; state->timeout_ms = ESP32_MQUICKJS_WIFI_RAW_TX_DEFAULT_TIMEOUT_MS;
    if (operation == SESSION_OPEN) {
        if (!capture_open_options(ctx, argc ? argv[0].val : JS_UNDEFINED, &state->options, &state->timeout_ms)) goto fail;
        state->handle = esp32_mquickjs_memory_wireless_calloc("wifi.raw-tx", 1, sizeof(*state->handle), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (state->handle == NULL) { JS_ThrowOutOfMemory(ctx); goto fail; }
        state->handle->references = 1;
        state->handle->options = state->options;
    } else {
        session_handle_t *handle = session_handle(ctx, receiver->val);
        if (handle == NULL) goto fail;
        if (operation == SESSION_CLOSE) {
            if (!handle_retain(handle)) { JS_ThrowOutOfMemory(ctx); goto fail; }
            state->handle = handle;
        }
        if (handle->native == NULL && operation != SESSION_CLOSE) {
            JS_ThrowReferenceError(ctx, "WiFiRawTxSession is closed"); goto fail;
        }
        if (handle->native != NULL) {
            if (!esp32_mquickjs_wifi_raw_tx_session_retain(handle->native)) { JS_ThrowOutOfMemory(ctx); goto fail; }
            state->session = handle->native;
        }
        state->options = handle->options;
        if (operation == SESSION_SEND) {
            if (!capture_send_options(ctx, argc > 1 ? argv[1].val : JS_UNDEFINED, &state->timeout_ms) ||
                !esp32_mquickjs_wifi_raw_tx_capture_bytes(ctx, &argv[0], session_operation_name(operation), &state->bytes, &state->length)) goto fail;
            esp32_mquickjs_wifi_raw_tx_validation_policy_t policy = {.interface = state->options.interface, .driver_sequence = state->options.driver_sequence};
            esp32_mquickjs_wifi_raw_tx_validated_frame_t frame;
            state->validation = esp32_mquickjs_wifi_raw_tx_validate(state->bytes, state->length, &policy, &frame);
            if (state->validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID) {
                session_error(ctx, session_operation_name(operation), "WIFI_RAW_TX_INVALID_FRAME", ESP_ERR_INVALID_ARG,
                    "capture", false, state->validation, ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID); goto fail;
            }
        } else if (operation == SESSION_WRITABLE) {
            if (!capture_writable_options(ctx, argc ? argv[0].val : JS_UNDEFINED, state)) goto fail;
        } else if (operation == SESSION_FLUSH && !capture_timeout(ctx, argc ? argv[0].val : JS_UNDEFINED, &state->timeout_ms)) goto fail;
    }
    *output = state; return true;
fail:
    session_future_destroy(state);
    return false;
}

#define CAPTURE(name, operation) \
static bool name(JSContext *ctx, JSGCRef *receiver, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **output) \
{ return session_capture(ctx, receiver, argc, argv, output, operation); }
CAPTURE(session_open_capture, SESSION_OPEN)
CAPTURE(session_send_capture, SESSION_SEND)
CAPTURE(session_flush_capture, SESSION_FLUSH)
CAPTURE(session_close_capture, SESSION_CLOSE)
CAPTURE(session_writable_capture, SESSION_WRITABLE)

static bool session_future_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    if (state->cancelled) return true;
    state->started = true;
    if (state->operation == SESSION_OPEN) {
        state->error = esp32_mquickjs_wifi_raw_tx_session_new(&state->options, &state->session);
        state->stage = "session-create";
    } else if (state->operation == SESSION_CLOSE) {
        if (state->session != NULL) esp32_mquickjs_wifi_raw_tx_session_request_close(state->session);
    } else if (state->operation == SESSION_FLUSH) {
        state->queue_result = esp32_mquickjs_wifi_raw_tx_session_flush_begin(state->session, &state->wait.flush);
        state->stage = "flush-admission";
    } else if (state->operation == SESSION_SEND) {
        payload_t frame = {state->bytes, (uint16_t)state->length};
        payload_t removed[ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_MAX_PACKETS] = {0};
        esp32_mquickjs_wifi_raw_tx_admission_t admission;
        state->queue_result = esp32_mquickjs_wifi_raw_tx_session_admit_result(state->session, &frame,
            removed, state->options.capacity, &admission, &state->validation, &state->wait.send.record, &state->wait.send.token);
        if (state->queue_result == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) {
            state->bytes = NULL; state->length = 0; state->admitted = true;
            for (unsigned i = 0; i < admission.evicted_packets; ++i) esp32_mquickjs_memory_payload_free(removed[i].data);
        }
        state->stage = "send-admission";
    }
    if (state->queue_result != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) state->error = ESP_ERR_INVALID_STATE;
    (void)esp32_mquickjs_wifi_raw_tx_sessions_service();
    return true;
}

static esp32_mquickjs_future_poll_t session_future_poll(esp32_mquickjs_future_driver_state_t *state)
{
    (void)esp32_mquickjs_wifi_raw_tx_sessions_service();
    if (state->cancelled || state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    if (!state->started) return ESP32_MQUICKJS_FUTURE_PENDING;
    bool ready = false, valid = true;
    if (state->operation == SESSION_OPEN || state->operation == SESSION_CLOSE) {
        native_status_t status = {0};
        if (state->session == NULL) ready = true;
        else if (!(valid = esp32_mquickjs_wifi_raw_tx_session_status(state->session, &status))) ready = true;
        else ready = state->operation == SESSION_OPEN ? status.open_complete || status.faulted || status.close_requested : status.closed;
    } else if (state->operation == SESSION_WRITABLE) {
        native_status_t status = {0};
        valid = esp32_mquickjs_wifi_raw_tx_session_status(state->session, &status);
        if (valid && (status.closed || status.close_requested || status.faulted)) {
            state->error = ESP_ERR_INVALID_STATE; state->stage = "queue-closed"; ready = true;
        } else if (valid && state->minimum_packets > status.remaining_sequences) {
            state->error = ESP_ERR_INVALID_STATE; state->stage = "queue-identity-exhausted"; ready = true;
        } else if (valid) ready = status.open_complete &&
            (uint32_t)(status.capacity - status.queued - status.in_flight) >= state->minimum_packets &&
            status.capacity_bytes - status.used_bytes >= state->minimum_bytes;
    } else if (state->operation == SESSION_SEND) {
        esp32_mquickjs_wifi_raw_tx_result_t result;
        valid = esp32_mquickjs_wifi_raw_tx_session_result_status(state->session, &state->wait.send.token, &result);
        ready = valid && result.kind != ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_PENDING;
    } else {
        esp32_mquickjs_wifi_raw_tx_flush_status_t result;
        valid = esp32_mquickjs_wifi_raw_tx_session_flush_status(state->session, &state->wait.flush, &result);
        ready = valid && result.pending == 0U;
    }
    if (!valid) { state->error = ESP_ERR_INVALID_STATE; state->stage = "wait-identity"; ready = true; }
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue totals_to_js(JSContext *ctx, const esp32_mquickjs_wifi_raw_tx_queue_totals_t *totals)
{
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
#define TOTAL(name) ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, #name, JS_NewUint32(ctx, totals->name), fail)
    TOTAL(admitted); TOTAL(submitted); TOTAL(settled); TOTAL(succeeded); TOTAL(failed);
    TOTAL(unknown); TOTAL(rejected); TOTAL(aborted); TOTAL(dropped);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "completed",
        JS_NewUint32(ctx, totals->succeeded + totals->failed + totals->unknown), fail);
#undef TOTAL
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue session_future_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return session_error(ctx, session_operation_name(state->operation), "WIFI_RAW_TX_SESSION_FAILED",
        state->error, state->stage, state->admitted, state->validation, state->queue_result);
    if (state->operation == SESSION_WRITABLE) return JS_UNDEFINED;
    if (state->operation == SESSION_CLOSE) {
        native_status_t status = {0};
        if (!handle_snapshot(state->handle, &status) || !status.closed)
            return session_error(ctx, session_operation_name(state->operation), "WIFI_RAW_TX_SESSION_FAILED", ESP_ERR_INVALID_STATE,
                "close-state", false, state->validation, state->queue_result);
        return JS_UNDEFINED;
    }
    if (state->operation == SESSION_OPEN) {
        native_status_t status = {0};
        if (!esp32_mquickjs_wifi_raw_tx_session_status(state->session, &status) || !status.open_complete || status.faulted || status.close_requested)
            return session_error(ctx, "wifi.rawTx.open", "WIFI_RAW_TX_SESSION_FAILED", status.error ? status.error : ESP_ERR_INVALID_STATE,
                status.stage ? status.stage : "open-state", false, state->validation, state->queue_result);
        JSValue object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_RAW_TX_SESSION);
        if (JS_IsException(object)) return JS_EXCEPTION;
        state->handle->native = state->session; state->session = NULL;
        JS_SetOpaque(ctx, object, state->handle); state->handle = NULL;
        return object;
    }
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    if (state->operation == SESSION_SEND) {
        esp32_mquickjs_wifi_raw_tx_result_t snapshot;
        if (!esp32_mquickjs_wifi_raw_tx_session_result_status(state->session, &state->wait.send.token, &snapshot)) goto stale;
        if (snapshot.kind != ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_COMPLETED) {
            session_error(ctx, "WiFiRawTxSession.send", snapshot.kind == ESP32_MQUICKJS_WIFI_RAW_TX_RESULT_UNCERTAIN
                ? "WIFI_RAW_TX_UNCERTAIN" : "WIFI_RAW_TX_SESSION_FAILED", snapshot.error ? snapshot.error : ESP_ERR_INVALID_STATE,
                snapshot.stage, true, state->validation, state->queue_result); goto fail;
        }
        *result = esp32_mquickjs_wifi_raw_tx_result_to_js(ctx, &snapshot.native, state->options.interface, snapshot.channel);
        if (JS_IsException(*result)) goto fail;
        ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "sessionGeneration", JS_NewUint32(ctx, snapshot.generation), fail);
        ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "admissionSequence", JS_NewUint32(ctx, snapshot.sequence), fail);
    } else {
        esp32_mquickjs_wifi_raw_tx_flush_status_t snapshot;
        if (!esp32_mquickjs_wifi_raw_tx_session_flush_status(state->session, &state->wait.flush, &snapshot)) goto stale;
        *result = totals_to_js(ctx, &snapshot.totals); if (JS_IsException(*result)) goto fail;
        ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "sessionGeneration", JS_NewUint32(ctx, snapshot.generation), fail);
        ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "throughSequence", JS_NewUint32(ctx, snapshot.fence), fail);
        ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "pending", JS_NewUint32(ctx, snapshot.pending), fail);
    }
    return JS_PopGCRef(ctx, &ref);
stale:
    session_error(ctx, session_operation_name(state->operation), "WIFI_RAW_TX_SESSION_FAILED", ESP_ERR_INVALID_STATE,
        "result-identity", state->admitted, state->validation, state->queue_result);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static esp32_mquickjs_cancel_result_t session_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    if (state->operation == SESSION_OPEN && state->session != NULL) esp32_mquickjs_wifi_raw_tx_session_request_close(state->session);
    return ESP32_MQUICKJS_CANCELLED;
}
static uint32_t session_future_timeout(const esp32_mquickjs_future_driver_state_t *state)
{
    return state->timeout_ms;
}
static void session_future_expire(esp32_mquickjs_future_driver_state_t *state)
{
    /* A close deadline must retain close intent even if the Future submission
     * queue has not dispatched start yet. Explicit cancel-before-start can still
     * withdraw that intent; a timeout is not an instruction to keep Radio open. */
    if (state->operation == SESSION_CLOSE && state->session != NULL)
        esp32_mquickjs_wifi_raw_tx_session_request_close(state->session);
    (void)session_future_cancel(state);
}
static JSValue session_future_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout)
{
    (void)timeout;
    session_future_expire(state);
    return session_error(ctx, session_operation_name(state->operation), "WIFI_RAW_TX_TIMEOUT", ESP_ERR_TIMEOUT,
        "deadline", state->admitted, state->validation, state->queue_result);
}
#define DRIVER(capture_name) { .memory_owner = "wireless.future", .capture = capture_name, .start = session_future_start, .poll = session_future_poll, \
    .finish = session_future_finish, .cancel = session_future_cancel, .destroy = session_future_destroy, \
    .timeout_ms = session_future_timeout, .on_timeout = session_future_on_timeout }
static const esp32_mquickjs_future_driver_t s_open_driver = DRIVER(session_open_capture);
static const esp32_mquickjs_future_driver_t s_send_driver = DRIVER(session_send_capture);
static const esp32_mquickjs_future_driver_t s_flush_driver = DRIVER(session_flush_capture);
static const esp32_mquickjs_future_driver_t s_close_driver = DRIVER(session_close_capture);
static const esp32_mquickjs_future_driver_t s_writable_driver = DRIVER(session_writable_capture);

static JSValue session_wait_call(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, session_operation_t operation)
{
    JSGCRef receiver_ref, owner_ref, method_ref;
    JSValue *receiver = JS_PushGCRef(ctx, &receiver_ref), *owner = JS_PushGCRef(ctx, &owner_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    *receiver = this_val ? *this_val : JS_UNDEFINED;
    *owner = JS_GetGlobalObject(ctx);
    if (operation == SESSION_OPEN) {
        *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "wifi");
        *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "rawTx");
        *receiver = *owner;
    } else {
        *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "WiFiRawTxSession");
        *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "prototype");
    }
    static const char *const methods[] = {"open", "send", "flush", "close", "waitWritable"};
    *method = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, methods[operation]);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *receiver, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &owner_ref); JS_PopGCRef(ctx, &receiver_ref);
    return result;
}
JSValue js_wifi_raw_tx_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return session_wait_call(ctx, this_val, argc, argv, SESSION_OPEN); }
JSValue js_wifi_raw_tx_session_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return session_wait_call(ctx, this_val, argc, argv, SESSION_SEND); }
JSValue js_wifi_raw_tx_session_flush(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return session_wait_call(ctx, this_val, argc, argv, SESSION_FLUSH); }
JSValue js_wifi_raw_tx_session_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return session_wait_call(ctx, this_val, argc, argv, SESSION_CLOSE); }

JSValue js_wifi_raw_tx_session_wait_writable(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return session_wait_call(ctx, this_val, argc, argv, SESSION_WRITABLE); }

static JSValue admission_to_js(JSContext *ctx, const esp32_mquickjs_wifi_raw_tx_admission_t *admission)
{
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "sessionGeneration", JS_NewUint32(ctx, admission->generation), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "firstSequence", JS_NewUint32(ctx, admission->first_sequence), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "lastSequence", JS_NewUint32(ctx, admission->last_sequence), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "batchSequence", JS_NewUint32(ctx, admission->batch_sequence), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "admittedPackets", JS_NewUint32(ctx, admission->admitted_packets), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "evictedPackets", JS_NewUint32(ctx, admission->evicted_packets), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "evictedBatches", JS_NewUint32(ctx, admission->evicted_batches), fail);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue session_enqueue(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv, bool batch)
{
    if (argc != 1 || this_val == NULL) return JS_ThrowTypeError(ctx, "Raw TX enqueue expects one frame or frame collection");
    JSGCRef self_ref, input_ref, field_ref, result_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *input = JS_PushGCRef(ctx, &input_ref);
    JSValue *field = JS_PushGCRef(ctx, &field_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *self = *this_val; *input = argv[0]; *result = JS_EXCEPTION;
    native_session_t *native = NULL;
    payload_t *frames = NULL, *removed = NULL;
    uint32_t count = 1, capacity = 0;
    session_handle_t *handle = session_handle(ctx, *self);
    if (handle == NULL) goto done;
    if (handle->native == NULL) { JS_ThrowReferenceError(ctx, "WiFiRawTxSession is closed"); goto done; }
    if (!esp32_mquickjs_wifi_raw_tx_session_retain(handle->native)) { JS_ThrowOutOfMemory(ctx); goto done; }
    native = handle->native;
    capacity = handle->options.capacity;
    const char *operation = batch ? "WiFiRawTxSession.enqueueBatch" : "WiFiRawTxSession.enqueue";
    if (batch) {
        if (JS_GetClassID(ctx, *input) < 0) { JS_ThrowTypeError(ctx, "enqueueBatch expects an ArrayLike of frames"); goto done; }
        *field = JS_GetPropertyStr(ctx, *input, "length");
        if (JS_IsException(*field)) goto done;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, capacity, &count)) {
            JS_ThrowTypeError(ctx, "batch length must be within the Session queue capacity"); goto done;
        }
    }
    frames = esp32_mquickjs_memory_wireless_calloc("wifi.raw-tx", count, sizeof(*frames), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    removed = esp32_mquickjs_memory_wireless_calloc("wifi.raw-tx", capacity, sizeof(*removed), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (frames == NULL || removed == NULL) { JS_ThrowOutOfMemory(ctx); goto done; }
    for (uint32_t i = 0; i < count; ++i) {
        *field = batch ? JS_GetPropertyUint32(ctx, *input, i) : *input;
        if (JS_IsException(*field)) goto done;
        size_t length = 0;
        if (!esp32_mquickjs_wifi_raw_tx_capture_bytes(ctx, &field_ref, operation, &frames[i].data, &length)) goto done;
        frames[i].length = (uint16_t)length;
    }
    esp32_mquickjs_wifi_raw_tx_admission_t admission;
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    esp32_mquickjs_wifi_raw_tx_queue_result_t admitted = esp32_mquickjs_wifi_raw_tx_session_admit(
        native, frames, (uint16_t)count, removed, (uint16_t)capacity, &admission, &validation);
    if (admitted != ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK) {
        session_error(ctx, operation, validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID ? "WIFI_RAW_TX_INVALID_FRAME" : "WIFI_RAW_TX_ADMISSION_FAILED",
            admitted == ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE,
            "queue-admission", false, validation, admitted); goto done;
    }
    /* Admission has happened. A result-construction exception cannot undo it. */
    *result = admission_to_js(ctx, &admission);
    (void)esp32_mquickjs_wifi_raw_tx_sessions_service();
done:
    if (frames != NULL) for (uint32_t i = 0; i < count; ++i) esp32_mquickjs_memory_payload_free(frames[i].data);
    if (removed != NULL) for (uint32_t i = 0; i < capacity; ++i) esp32_mquickjs_memory_payload_free(removed[i].data);
    esp32_mquickjs_memory_payload_free(frames); esp32_mquickjs_memory_payload_free(removed);
    esp32_mquickjs_wifi_raw_tx_session_release(native);
    JSValue value = JS_PopGCRef(ctx, &result_ref);
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &input_ref); JS_PopGCRef(ctx, &self_ref);
    return value;
}
JSValue js_wifi_raw_tx_session_enqueue(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return session_enqueue(ctx, this_val, argc, argv, false); }
JSValue js_wifi_raw_tx_session_enqueue_batch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ return session_enqueue(ctx, this_val, argc, argv, true); }

JSValue js_wifi_raw_tx_session_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || this_val == NULL) return JS_ThrowTypeError(ctx, "WiFiRawTxSession.status expects no arguments");
    session_handle_t *handle = session_handle(ctx, *this_val);
    native_status_t status;
    if (handle == NULL) return JS_EXCEPTION;
    if (!handle_snapshot(handle, &status)) return JS_ThrowInternalError(ctx, "Raw TX status unavailable");
    /* No handle/native pointer is accessed after the first JS allocation. */
    session_options_t options = handle->options;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "state", JS_NewString(ctx, status.closed ? "closed" : status.faulted ? "faulted" : status.close_requested ? "closing" : "open"), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "sessionGeneration", JS_NewUint32(ctx, status.generation), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "radioGeneration", status.radio_generation ? JS_NewUint32(ctx, status.radio_generation) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "interface", JS_NewString(ctx, options.interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION ? "station" : "access-point"), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "channel", status.channel ? JS_NewUint32(ctx, status.channel) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "sequenceControl", JS_NewString(ctx, options.driver_sequence ? "driver" : "application"), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "capacityPackets", JS_NewUint32(ctx, status.capacity), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "capacityBytes", JS_NewUint32(ctx, status.capacity_bytes), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "usedBytes", JS_NewUint32(ctx, status.used_bytes), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "availableBytes", JS_NewUint32(ctx, status.capacity_bytes - status.used_bytes), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "availablePackets", JS_NewUint32(ctx, status.capacity - status.queued - status.in_flight), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "highWaterBytes", JS_NewUint32(ctx, status.high_water_bytes), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "inFlight", JS_NewUint32(ctx, status.in_flight), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "maxInFlight", JS_NewUint32(ctx, status.max_in_flight), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "queuedPackets", JS_NewUint32(ctx, status.queued), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "pendingPackets", JS_NewUint32(ctx, status.totals.admitted - status.totals.settled), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "activeSequence", status.active_sequence ? JS_NewUint32(ctx, status.active_sequence) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "requestIdentity", status.lane_identity ? JS_NewUint32(ctx, status.lane_identity) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "workerBusy", JS_NewBool(status.worker_busy), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "periodicJobs", JS_NewUint32(ctx, status.periodic_children), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "faulted", JS_NewBool(status.faulted), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "cleanupPending", JS_NewBool(status.close_requested && !status.closed), fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "error", status.error ? JS_NewInt32(ctx, status.error) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "stage", status.stage ? JS_NewString(ctx, status.stage) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "cleanupError", status.cleanup_error ? JS_NewInt32(ctx, status.cleanup_error) : JS_NULL, fail);
    ESP32_MQUICKJS_SET_OR_GOTO(ctx, result, "cleanupStage", status.cleanup_stage ? JS_NewString(ctx, status.cleanup_stage) : JS_NULL, fail);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_raw_tx_session_stats(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || this_val == NULL) return JS_ThrowTypeError(ctx, "WiFiRawTxSession.stats expects no arguments");
    session_handle_t *handle = session_handle(ctx, *this_val);
    native_status_t status;
    if (handle == NULL) return JS_EXCEPTION;
    if (!handle_snapshot(handle, &status)) return JS_ThrowInternalError(ctx, "Raw TX stats unavailable");
    return totals_to_js(ctx, &status.totals);
}

bool esp32_mquickjs_init_wifi_raw_tx_session_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef owner_ref, method_ref;
    JSValue *owner = JS_PushGCRef(ctx, &owner_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    *owner = JS_GetGlobalObject(ctx);
    *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "wifi");
    *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "rawTx");
    *method = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "open");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_open_driver)) goto done;
    *owner = JS_GetGlobalObject(ctx);
    *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "WiFiRawTxSession");
    *owner = JS_IsException(*owner) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *owner, "prototype");
    if (JS_IsException(*owner)) goto done;
    static const char *const methods[] = {"send", "flush", "close", "waitWritable"};
    static const esp32_mquickjs_future_driver_t *const drivers[] = {&s_send_driver, &s_flush_driver, &s_close_driver, &s_writable_driver};
    for (unsigned i = 0; i < 4; ++i) {
        *method = JS_GetPropertyStr(ctx, *owner, methods[i]);
        if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, drivers[i])) goto done;
    }
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register Raw TX Session drivers");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &owner_ref);
    return ok;
}
#endif
