#include "esp32_mquickjs_wifi_raw_tx.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_wifi_raw_tx_lane.h"
#include "esp32_mquickjs_wifi_raw_tx_session.h"
#include "esp32_mquickjs_wifi_raw_tx_periodic_job.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_memory.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include <stdatomic.h>
#include <string.h>

typedef struct {
    esp32_mquickjs_wifi_raw_tx_interface_t interface;
    uint8_t channel;
    bool driver_sequence;
    uint32_t timeout_ms;
} raw_tx_options_t;

struct esp32_mquickjs_future_driver_state {
    raw_tx_options_t options;
    uint8_t *bytes;
    size_t length;
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp32_mquickjs_wifi_raw_tx_token_t native_token;
    esp32_mquickjs_wifi_raw_tx_broker_status_t result;
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    esp_err_t error;
    const char *stage;
    uint8_t channel;
    esp32_mquickjs_wifi_raw_tx_lane_token_t lane;
    bool submitted, lane_acquired, schedule_failed;
    atomic_bool worker_done, cancel_requested;
};

/* One native retirement slot, independent of Future storage and JS roots.
 * A Future lane cannot start the next send until this exact owner is cleaned.
 * No runtime/task/JS pointer is retained by this slot or the SDK callback. */
static portMUX_TYPE s_retired_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    esp32_mquickjs_wifi_radio_lease_t lease;
    esp32_mquickjs_wifi_raw_tx_token_t token;
    esp32_mquickjs_wifi_raw_tx_lane_token_t lane;
    bool active, busy, stop_pending;
    esp_err_t error;
    const char *stage;
    int64_t next_retry_us;
} s_retired;
static const char s_raw_tx_lane;

static bool raw_tx_retired_pending(void)
{
    portENTER_CRITICAL(&s_retired_lock);
    bool active = s_retired.active;
    portEXIT_CRITICAL(&s_retired_lock);
    return active;
}

static void raw_tx_cleanup_worker(void *opaque)
{
    (void)opaque;
    portENTER_CRITICAL(&s_retired_lock);
    esp32_mquickjs_wifi_radio_lease_t lease = s_retired.lease;
    esp32_mquickjs_wifi_raw_tx_token_t token = s_retired.token;
    esp32_mquickjs_wifi_raw_tx_lane_token_t lane = s_retired.lane;
    bool stop_pending = s_retired.stop_pending;
    portEXIT_CRITICAL(&s_retired_lock);
    esp_err_t error = ESP_OK;
    const char *stage = NULL;
    if (token.identity != 0U && !esp32_mquickjs_wifi_radio_raw_tx_retire(&lease, &token)) {
        error = ESP_ERR_INVALID_STATE;
        stage = "native-completion";
    }
    if (token.identity == 0U && (lease.acquired || stop_pending)) {
        error = esp32_mquickjs_wifi_radio_release_and_stop_idle(&lease);
        if (error != ESP_OK || lease.acquired) stage = "radio-release-stop";
        if (lease.acquired && error == ESP_OK) error = ESP_ERR_INVALID_STATE;
        stop_pending = error != ESP_OK;
    }
    if (error == ESP_OK && token.identity == 0U && !lease.acquired && !stop_pending &&
        lane.identity != 0U && !esp32_mquickjs_wifi_raw_tx_lane_release(&lane)) {
        error = ESP_ERR_INVALID_STATE;
        stage = "lane-release";
    }
    int64_t retry = esp_timer_get_time() + 100000;
    portENTER_CRITICAL(&s_retired_lock);
    s_retired.lease = lease;
    s_retired.token = token;
    s_retired.lane = lane;
    s_retired.stop_pending = stop_pending;
    s_retired.error = error;
    s_retired.stage = stage;
    s_retired.active = token.identity != 0U || lease.acquired || lane.identity != 0U || error != ESP_OK;
    s_retired.next_retry_us = retry;
    s_retired.busy = false;
    portEXIT_CRITICAL(&s_retired_lock);
}

static bool raw_tx_service(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, void *opaque)
{
    (void)ctx; (void)runtime; (void)opaque;
    bool ap_handled = esp32_mquickjs_wifi_raw_tx_sessions_runtime_service();
    bool periodic_handled = esp32_mquickjs_wifi_raw_tx_periodic_jobs_service();
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_retired_lock);
    bool submit = s_retired.active && !s_retired.busy && now >= s_retired.next_retry_us;
    if (submit) s_retired.busy = true;
    portEXIT_CRITICAL(&s_retired_lock);
    if (!submit) {
        bool sessions_handled = esp32_mquickjs_wifi_raw_tx_sessions_service();
        return ap_handled || periodic_handled || sessions_handled;
    }
    if (!esp32_mquickjs_submit_background_worker(raw_tx_cleanup_worker, NULL)) {
        portENTER_CRITICAL(&s_retired_lock);
        s_retired.busy = false;
        s_retired.next_retry_us = now + 100000;
        portEXIT_CRITICAL(&s_retired_lock);
        return ap_handled || periodic_handled;
    }
    (void)esp32_mquickjs_wifi_raw_tx_sessions_service();
    return true;
}

bool esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy(void)
{
    esp32_mquickjs_wifi_raw_tx_sessions_request_close();
    (void)raw_tx_service(NULL, NULL, NULL);
    return !raw_tx_retired_pending() && esp32_mquickjs_wifi_raw_tx_sessions_drained();
}

static JSValue raw_tx_error(JSContext *ctx, const char *code, esp_err_t error,
    const char *stage, esp32_mquickjs_wifi_raw_tx_validation_t validation)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, error)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(error))) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", stage ? JS_NewString(ctx, stage) : JS_NULL) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "validationCode", JS_NewUint32(ctx, validation))) {
        JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, code, "wifi.rawTx.send",
        "Raw TX did not complete; inspect wifi.status().radio.rawTx for native ownership", *details);
    JS_PopGCRef(ctx, &ref);
    return result;
}

static bool raw_tx_capture_options(JSContext *ctx, JSValue value, raw_tx_options_t *output)
{
    static const char *const keys[] = {"interface", "channel", "sequenceControl", "validation", "timeoutMs"};
    static const char *const interfaces[] = {"station", "access-point"};
    static const char *const sequences[] = {"driver", "application"};
    static const char *const validations[] = {"strict", "basic"};
    static const char *const current[] = {"current"};
    raw_tx_options_t options = {.driver_sequence = true, .timeout_ms = 1000};
    if (JS_IsUndefined(value)) { *output = options; return true; }
    JSGCRef root_ref, field_ref;
    JSValue *root = JS_PushGCRef(ctx, &root_ref), *field = JS_PushGCRef(ctx, &field_ref);
    *root = value;
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, *root, "wifi.rawTx.send", keys, 5)) goto done;
    size_t choice;
    uint32_t number;
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
            if (number > 14) {
#if CONFIG_SOC_WIFI_SUPPORT_5G
                if (esp32_mquickjs_wifi_radio_5ghz_channel_bit(number) == 0) goto invalid;
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
        options.driver_sequence = choice == 0;
    }
    *field = JS_GetPropertyStr(ctx, *root, "validation");
    if (JS_IsException(*field)) goto done;
    /* Both policies retain every mandatory MAC/SDK constraint. Neither option
     * authorizes invalid frames or promises arbitrary body/FCS recognition. */
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_enum(ctx, *field, validations, 2, &choice)) goto invalid;
    *field = JS_GetPropertyStr(ctx, *root, "timeoutMs");
    if (JS_IsException(*field)) goto done;
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 60000, &options.timeout_ms)) goto invalid;
    *output = options;
    ok = true;
    goto done;
invalid:
    JS_ThrowTypeError(ctx, "invalid wifi.rawTx.send options");
done:
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &root_ref);
    return ok;
}

bool esp32_mquickjs_wifi_raw_tx_capture_bytes(JSContext *ctx, JSGCRef *value,
    const char *operation, uint8_t **bytes, size_t *byte_length)
{
    if (value == NULL || operation == NULL || bytes == NULL || byte_length == NULL ||
        *bytes != NULL || *byte_length != 0U) {
        JS_ThrowInternalError(ctx, "invalid Raw TX capture output"); return false;
    }
    const uint8_t *data = NULL;
    size_t length = 0;
    bool leased = false, ok = false;
    JSGCRef field_ref;
    JSValue *field = JS_PushGCRef(ctx, &field_ref);
    if (JS_GetClassID(ctx, value->val) == JS_CLASS_BYTE_VIEW) {
        if (!esp32_mquickjs_byte_view_acquire_read(ctx, value->val, operation, &data, &length)) goto done;
        leased = true;
    } else {
        if (JS_GetClassID(ctx, value->val) < 0) goto invalid;
        *field = JS_GetPropertyStr(ctx, value->val, "length");
        if (JS_IsException(*field)) goto done;
        uint32_t count;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 24, 1500, &count)) goto invalid;
        length = count;
    }
    if (length < 24 || length > 1500) goto invalid;
    (*bytes) = esp32_mquickjs_memory_wireless_alloc("wifi.raw-tx", length, ESP32_MQUICKJS_MEMORY_EXTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    if ((*bytes) == NULL) { JS_ThrowOutOfMemory(ctx); goto done; }
    (*byte_length) = length;
    if (leased) memcpy((*bytes), data, length);
    else for (uint32_t i = 0; i < length; ++i) {
        *field = JS_GetPropertyUint32(ctx, value->val, i);
        if (JS_IsException(*field)) goto done;
        uint32_t byte;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0, 255, &byte)) goto invalid;
        (*bytes)[i] = byte;
    }
    ok = true;
    goto done;
invalid:
    JS_ThrowTypeError(ctx, "%s expects a 24-1500 byte ByteSource", operation);
done:
    if (leased) esp32_mquickjs_byte_view_release_read(ctx, value->val);
    JS_PopGCRef(ctx, &field_ref);
    return ok;
}

static bool raw_tx_capture_bytes(JSContext *ctx, JSGCRef *value,
    esp32_mquickjs_future_driver_state_t *state)
{
    return esp32_mquickjs_wifi_raw_tx_capture_bytes(ctx, value, "wifi.rawTx.send", &state->bytes, &state->length);
}

static bool raw_tx_capture(JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    (void)this_ref;
    *output = NULL;
    if (argc < 1 || argc > 2) { JS_ThrowTypeError(ctx, "wifi.rawTx.send expects frame and optional options"); return false; }
    raw_tx_options_t options;
    if (!raw_tx_capture_options(ctx, argc > 1 ? argv[1].val : JS_UNDEFINED, &options)) return false;
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); return false; }
    state->options = options;
    atomic_init(&state->worker_done, false);
    atomic_init(&state->cancel_requested, false);
    if (!raw_tx_capture_bytes(ctx, &argv[0], state)) goto fail;
    esp32_mquickjs_wifi_raw_tx_validation_policy_t policy = {.interface = options.interface, .driver_sequence = options.driver_sequence};
    esp32_mquickjs_wifi_raw_tx_validated_frame_t frame;
    state->validation = esp32_mquickjs_wifi_raw_tx_validate(state->bytes, state->length, &policy, &frame);
    if (state->validation != ESP32_MQUICKJS_WIFI_RAW_TX_VALID) {
        raw_tx_error(ctx, "WIFI_RAW_TX_INVALID_FRAME", ESP_ERR_INVALID_ARG, "capture", state->validation);
        goto fail;
    }
    *output = state;
    return true;
fail:
    esp32_mquickjs_memory_payload_free(state->bytes);
    esp32_mquickjs_memory_payload_free(state);
    return false;
}

static void raw_tx_send_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    if (!atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) {
        state->stage = "radio-acquire";
        state->error = esp32_mquickjs_wifi_radio_raw_tx_acquire(state->options.interface,
            state->options.channel, NULL, &state->lease, &state->channel);
        if (state->error == ESP_OK && !atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) {
            state->stage = "submit";
            state->error = esp32_mquickjs_wifi_radio_raw_tx_submit(&state->lease, state->options.interface,
                state->options.driver_sequence, state->bytes, state->length, &state->native_token,
                &state->validation, &state->channel);
        }
    }
    /* Final access to state: the worker queue has no runtime wake/context and
     * the Future may release state as soon as it observes this publication. */
    atomic_store_explicit(&state->worker_done, true, memory_order_release);
}

static void raw_tx_schedule(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->submitted || state->schedule_failed || atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return;
    (void)raw_tx_service(NULL, NULL, NULL);
    if (raw_tx_retired_pending()) return;
    if (state->lane.identity == 0U) {
        esp32_mquickjs_wifi_raw_tx_lane_result_t admission = esp32_mquickjs_wifi_raw_tx_lane_request(&state->lane);
        if (admission == ESP32_MQUICKJS_WIFI_RAW_TX_LANE_FULL) return;
        if (admission != ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK) {
            state->schedule_failed = true;
            state->error = ESP_ERR_INVALID_STATE;
            state->stage = admission == ESP32_MQUICKJS_WIFI_RAW_TX_LANE_EXHAUSTED ? "lane-identity-exhausted" : "lane-request";
            return;
        }
    }
    if (!state->lane_acquired) state->lane_acquired = esp32_mquickjs_wifi_raw_tx_lane_acquire(&state->lane);
    if (!state->lane_acquired) return;
    /* Runtime-free worker items avoid a delayed pool wake referencing a runtime
     * after terminal state publication. Future's normal polling observes done. */
    state->submitted = esp32_mquickjs_submit_background_worker(raw_tx_send_worker, state);
}

static bool raw_tx_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    raw_tx_schedule(state);
    return true;
}

static esp32_mquickjs_future_poll_t raw_tx_poll(esp32_mquickjs_future_driver_state_t *state)
{
    raw_tx_schedule(state);
    if (state->submitted && !atomic_load_explicit(&state->worker_done, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_READY;
    if (state->schedule_failed) return ESP32_MQUICKJS_FUTURE_READY;
    if (!state->submitted) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    esp32_mquickjs_wifi_raw_tx_broker_status(&state->result);
    if (state->result.token.identity != state->native_token.identity ||
        state->result.token.generation != state->native_token.generation ||
        state->result.token.radio_lease_identity != state->native_token.radio_lease_identity ||
        state->native_token.identity == 0U) {
        state->error = ESP_ERR_INVALID_STATE; state->stage = "completion-identity";
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->result.native_terminated) {
        state->error = ESP_ERR_INVALID_STATE; state->stage = "native-terminated";
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->result.correlation_fault) {
        state->error = ESP_ERR_INVALID_STATE; state->stage = "completion-correlation";
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    return state->result.driver_completed && state->result.callbacks_active == 0U
        ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}

#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)
JSValue esp32_mquickjs_wifi_raw_tx_result_to_js(JSContext *ctx,
    const esp32_mquickjs_wifi_raw_tx_broker_status_t *native,
    esp32_mquickjs_wifi_raw_tx_interface_t interface, uint8_t channel)
{
    static const char *const frame_types[] = {"beacon", "probe-request", "probe-response", "action", "non-qos-data"};
    if (native == NULL || native->native_terminated || (unsigned)native->frame_type >= 5U) return JS_ThrowInternalError(ctx, "invalid Raw TX completion frame type");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, native->token.identity));
    SET(result, "radioGeneration", JS_NewUint32(ctx, native->token.generation));
    SET(result, "interface", JS_NewString(ctx, interface == ESP32_MQUICKJS_WIFI_RAW_TX_STATION ? "station" : "access-point"));
    SET(result, "channel", JS_NewUint32(ctx, channel));
    SET(result, "frameType", JS_NewString(ctx, frame_types[native->frame_type]));
    SET(result, "byteLength", JS_NewUint32(ctx, native->byte_length));
    SET(result, "submittedAtUs", JS_NewFloat64(ctx, (double)native->submitted_at_us));
    SET(result, "completedAtUs", JS_NewFloat64(ctx, (double)native->completion.callback_time_us));
    SET(result, "driverAccepted", JS_NewBool(native->driver_accepted));
    SET(result, "driverCompleted", JS_NewBool(native->driver_completed));
    SET(result, "driverStatus", JS_NewString(ctx, native->completion.status == ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS ? "success" :
        native->completion.status == ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED ? "failed" : "unknown"));
    const char *rate = native->driver_completed ? esp32_mquickjs_wifi_tx_rate_name(native->completion.raw_rate) : NULL;
    SET(result, "rate", rate != NULL ? JS_NewString(ctx, rate) : JS_NULL);
    SET(result, "rawRate", JS_NewInt32(ctx, native->completion.raw_rate));
    SET(result, "rawStatus", JS_NewInt32(ctx, native->completion.raw_status));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue raw_tx_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return raw_tx_error(ctx, "WIFI_RAW_TX_SEND_FAILED", state->error, state->stage, state->validation);
    if ((unsigned)state->result.frame_type >= 5U) return raw_tx_error(ctx, "WIFI_RAW_TX_SEND_FAILED", ESP_ERR_INVALID_STATE, "frame-type", state->validation);
    return esp32_mquickjs_wifi_raw_tx_result_to_js(ctx, &state->result, state->options.interface, state->channel);
}

static esp32_mquickjs_cancel_result_t raw_tx_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    /* Cancellation stops waiting; already submitted RF may still occur. Only
     * the Future-owned worker needs to finish before native ownership transfers. */
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}

static void raw_tx_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    if (state->native_token.identity != 0U) (void)esp32_mquickjs_wifi_raw_tx_broker_abandon(&state->native_token);
    /* A waiter never reached Radio. A grant with no acquired Radio lease is
     * also safe to return immediately, even when the worker queue is full. */
    if (state->lane.identity != 0U && !state->lane_acquired)
        (void)esp32_mquickjs_wifi_raw_tx_lane_withdraw(&state->lane);
    if (state->lane_acquired && !state->lease.acquired &&
        esp32_mquickjs_wifi_raw_tx_lane_release(&state->lane)) state->lane_acquired = false;
    if (state->lease.acquired || state->lane_acquired) {
        portENTER_CRITICAL(&s_retired_lock);
        /* The single Future lane admits sends only after this slot drains. */
        s_retired.lease = state->lease;
        s_retired.token = state->native_token;
        s_retired.lane = state->lane;
        s_retired.stop_pending = state->lease.acquired;
        s_retired.active = true;
        s_retired.error = ESP_OK;
        s_retired.stage = "native-completion";
        s_retired.next_retry_us = 0;
        portEXIT_CRITICAL(&s_retired_lock);
    }
    esp32_mquickjs_memory_payload_free(state->bytes);
    esp32_mquickjs_memory_payload_free(state);
    (void)raw_tx_service(NULL, NULL, NULL);
}

static uint32_t raw_tx_timeout(const esp32_mquickjs_future_driver_state_t *state)
{
    return state->options.timeout_ms;
}
static JSValue raw_tx_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    /* Worker-owned scalar fields are not read until worker_done. */
    return raw_tx_error(ctx, "WIFI_RAW_TX_TIMEOUT", ESP_ERR_TIMEOUT, "deadline", ESP32_MQUICKJS_WIFI_RAW_TX_VALID);
}
static esp32_mquickjs_resource_key_t raw_tx_resource(const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return &s_raw_tx_lane;
}
static const esp32_mquickjs_future_driver_t s_raw_tx_driver = {
    .memory_owner = "wireless.future", .capture = raw_tx_capture, .start = raw_tx_start, .poll = raw_tx_poll, .finish = raw_tx_finish,
    .cancel = raw_tx_cancel, .destroy = raw_tx_destroy, .timeout_ms = raw_tx_timeout,
    .on_timeout = raw_tx_on_timeout, .resource_key = raw_tx_resource,
};

JSValue esp32_mquickjs_wifi_raw_tx_status(JSContext *ctx)
{
    esp32_mquickjs_wifi_raw_tx_broker_status_t native;
    esp32_mquickjs_wifi_raw_tx_broker_status(&native);
    esp32_mquickjs_wifi_raw_tx_lane_status_t lane;
    esp32_mquickjs_wifi_raw_tx_lane_status(&lane);
    esp32_mquickjs_wifi_raw_tx_sessions_status_t sessions;
    esp32_mquickjs_wifi_raw_tx_sessions_status(&sessions);
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t periodic;
    esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(&periodic);
    portENTER_CRITICAL(&s_retired_lock);
    bool retired = s_retired.active;
    esp_err_t error = s_retired.error;
    const char *stage = s_retired.stage;
    portEXIT_CRITICAL(&s_retired_lock);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "operationActive", JS_NewBool(native.operation_active));
    SET(result, "operationIdentity", native.operation_active || native.native_terminated ? JS_NewUint32(ctx, native.token.identity) : JS_NULL);
    SET(result, "radioGeneration", native.operation_active || native.native_terminated ? JS_NewUint32(ctx, native.token.generation) : JS_NULL);
    SET(result, "nativeTerminated", JS_NewBool(native.native_terminated));
    SET(result, "terminatedRadioGeneration", native.native_terminated ? JS_NewUint32(ctx, native.token.generation) : JS_NULL);
    SET(result, "quarantined", JS_NewBool(native.quarantined || native.registration_uncertain || native.unregister_written));
    SET(result, "correlationFault", JS_NewBool(native.correlation_fault));
    SET(result, "cleanupPending", JS_NewBool(native.native_terminated || retired || sessions.closing != 0U || periodic.cleanup_pending != 0U));
    SET(result, "cleanupStage", stage ? JS_NewString(ctx, stage) : JS_NULL);
    SET(result, "cleanupError", error != ESP_OK ? JS_NewInt32(ctx, error) : JS_NULL);
    SET(result, "submitError", native.submit_error != ESP_OK ? JS_NewInt32(ctx, native.submit_error) : JS_NULL);
    SET(result, "registrationError", native.cleanup_error != ESP_OK ? JS_NewInt32(ctx, native.cleanup_error) : JS_NULL);
    SET(result, "identityExhausted", JS_NewBool(native.identity_exhausted));
    SET(result, "laneIdentity", lane.active_identity ? JS_NewUint32(ctx, lane.active_identity) : JS_NULL);
    SET(result, "laneWaiters", JS_NewUint32(ctx, lane.waiting));
    SET(result, "laneIdentityExhausted", JS_NewBool(lane.identity_exhausted));
    SET(result, "liveSessions", JS_NewUint32(ctx, sessions.live));
    SET(result, "retainedClosedSessions", JS_NewUint32(ctx, sessions.closed));
    SET(result, "closingSessions", JS_NewUint32(ctx, sessions.closing));
    SET(result, "faultedSessions", JS_NewUint32(ctx, sessions.faulted));
    SET(result, "registeredSessionResults", JS_NewUint32(ctx, sessions.pending_results));
    SET(result, "registeredSessionFlushes", JS_NewUint32(ctx, sessions.pending_flushes));
    SET(result, "sessionErrorGeneration", sessions.error_generation ? JS_NewUint32(ctx, sessions.error_generation) : JS_NULL);
    SET(result, "sessionError", sessions.error ? JS_NewInt32(ctx, sessions.error) : JS_NULL);
    SET(result, "sessionStage", sessions.stage ? JS_NewString(ctx, sessions.stage) : JS_NULL);
    SET(result, "sessionCleanupError", sessions.cleanup_error ? JS_NewInt32(ctx, sessions.cleanup_error) : JS_NULL);
    SET(result, "sessionCleanupStage", sessions.cleanup_stage ? JS_NewString(ctx, sessions.cleanup_stage) : JS_NULL);
    SET(result, "periodicJobs", JS_NewUint32(ctx, periodic.live));
    SET(result, "retiredPeriodicJobs", JS_NewUint32(ctx, periodic.retired));
    SET(result, "faultedPeriodicJobs", JS_NewUint32(ctx, periodic.faulted));
    SET(result, "periodicCleanupPending", JS_NewUint32(ctx, periodic.cleanup_pending));
    SET(result, "periodicIdentityExhausted", JS_NewBool(periodic.identity_exhausted));
    SET(result, "periodicErrorGeneration", periodic.error_generation ? JS_NewUint32(ctx, periodic.error_generation) : JS_NULL);
    SET(result, "periodicError", periodic.error ? JS_NewInt32(ctx, periodic.error) : JS_NULL);
    SET(result, "periodicStage", periodic.stage ? JS_NewString(ctx, periodic.stage) : JS_NULL);
    SET(result, "periodicCleanupError", periodic.cleanup_error ? JS_NewInt32(ctx, periodic.cleanup_error) : JS_NULL);
    SET(result, "periodicCleanupStage", periodic.cleanup_stage ? JS_NewString(ctx, periodic.cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_raw_tx_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "rawTx");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "send");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *module, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_wifi_raw_tx_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.rawTx.capabilities expects no arguments");
    JSGCRef result_ref, child_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref), *child = JS_PushGCRef(ctx, &child_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-raw-tx/1"));
    SET(result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
    SET(result, "idfVersion", JS_NewString(ctx, esp_get_idf_version()));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    *child = JS_NewArray(ctx, 0);
    if (JS_IsException(*child)) goto fail;
    *item = JS_NewString(ctx, "station");
    if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *child, 0, *item))) goto fail;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    *item = JS_NewString(ctx, "access-point");
    if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *child, 1, *item))) goto fail;
#endif
    SET(result, "interfaces", *child);
    *child = JS_NewArray(ctx, 0);
    if (JS_IsException(*child)) goto fail;
    *item = JS_NewString(ctx, "station");
    if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *child, 0, *item))) goto fail;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    *item = JS_NewString(ctx, "access-point");
    if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *child, 1, *item))) goto fail;
#endif
    SET(result, "rateLeaseInterfaces", *child);
    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    SET(child, "beacon", JS_TRUE); SET(child, "probeRequest", JS_TRUE); SET(child, "probeResponse", JS_TRUE);
    SET(child, "action", JS_TRUE); SET(child, "nonQosData", JS_TRUE);
    SET(child, "qosData", JS_FALSE); SET(child, "encryptedData", JS_FALSE); SET(child, "arbitraryControl", JS_FALSE);
    SET(result, "frameTypes", *child);
    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    SET(child, "driverSequence", JS_TRUE); SET(child, "applicationSequence", JS_TRUE);
    SET(child, "txDoneCallback", JS_TRUE); SET(child, "fixedChannel", JS_TRUE);
    SET(child, "nativeQueue", JS_TRUE); SET(child, "batchAdmission", JS_TRUE);
    SET(child, "periodicTx", JS_TRUE); SET(child, "rateLease", JS_TRUE);
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C5
    SET(child, "recovery", JS_TRUE);
#else
    SET(child, "recovery", JS_FALSE);
#endif
    SET(child, "callerFcs", JS_FALSE);
    SET(result, "supports", *child);
    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    SET(child, "minimumFrameBytes", JS_NewInt32(ctx, 24)); SET(child, "maximumFrameBytes", JS_NewInt32(ctx, 1500));
    SET(child, "maximumQueueCapacity", JS_NewInt32(ctx, 128)); SET(child, "maximumBatchFrames", JS_NewInt32(ctx, 128));
    SET(child, "maximumSessions", JS_NewInt32(ctx, 8));
    SET(child, "maximumPendingResultsPerSession", JS_NewInt32(ctx, 8));
    SET(child, "maximumPendingFlushesPerSession", JS_NewInt32(ctx, 8));
    SET(child, "minimumPeriodicIntervalUs", JS_NewInt32(ctx, ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_MIN_INTERVAL_US));
    SET(child, "maximumPeriodicJobs", JS_NewInt32(ctx, ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS));
    SET(result, "limits", *child);
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &child_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &child_ref); JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

bool esp32_mquickjs_init_wifi_raw_tx_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "rawTx");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "send");
    bool ok = !JS_IsException(*method) && esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_raw_tx_driver) &&
        esp32_mquickjs_register_async_poller(runtime, raw_tx_service, NULL) &&
        esp32_mquickjs_init_wifi_raw_tx_session_runtime(ctx, runtime) &&
        esp32_mquickjs_init_wifi_raw_tx_periodic_runtime(ctx, runtime) &&
        esp32_mquickjs_init_wifi_raw_tx_recovery_runtime(ctx, runtime);
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register Raw TX runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
