#include "esp32_mquickjs_wifi_action.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_action_radio.h"
#include "esp32_mquickjs_wifi_roc_session.h"
#include "esp32_mquickjs_wifi_action_sdk.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_memory.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <string.h>

typedef struct {
    wifi_action_tx_req_t request;
    uint32_t timeout_ms;
} action_options_t;

struct esp32_mquickjs_future_driver_state {
    wifi_action_tx_req_t *request;
    size_t request_bytes;
    uint32_t timeout_ms;
    esp32_mquickjs_wifi_action_token_t token;
    esp32_mquickjs_wifi_action_lane_t result;
    esp_err_t error;
    const char *stage;
    bool submitted;
    atomic_bool worker_done, cancel_requested;
};

/* Boot-owned scalar retirement. Future roots/storage never enter this record.
 * One Future resource lane prevents overwriting it; new work waits for drain. */
static portMUX_TYPE s_action_retired_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    esp32_mquickjs_wifi_action_token_t token;
    bool active, busy, cancel;
    esp_err_t error;
    const char *stage;
    int64_t next_retry_us;
} s_action_retired;
static const char s_action_future_lane;

static bool action_retired_pending(void)
{
    portENTER_CRITICAL(&s_action_retired_lock);
    bool pending = s_action_retired.active;
    portEXIT_CRITICAL(&s_action_retired_lock);
    return pending;
}

static void action_cleanup_worker(void *opaque)
{
    (void)opaque;
    portENTER_CRITICAL(&s_action_retired_lock);
    esp32_mquickjs_wifi_action_token_t token = s_action_retired.token;
    bool cancel = s_action_retired.cancel;
    portEXIT_CRITICAL(&s_action_retired_lock);
    esp_err_t error = ESP_OK;
    const char *stage = NULL;
    esp32_mquickjs_wifi_action_lane_t native = {0};
    if (!esp32_mquickjs_wifi_radio_action_status(&token, &native)) {
        error = ESP_ERR_INVALID_STATE;
        stage = "cleanup-identity";
    } else {
        if (cancel && !native.terminal && !native.cancel_written) {
            error = esp32_mquickjs_wifi_radio_action_cancel(&token);
            if (error != ESP_OK) stage = "cancel";
        }
        /* A failed cancel does not prevent observing a later natural terminal.
         * Successful native cancellation is recorded in Radio, never reissued. */
        esp_err_t retired = esp32_mquickjs_wifi_radio_action_retire(&token, &native);
        if (retired == ESP_OK) { error = ESP_OK; stage = NULL; }
        else if (error == ESP_OK) { error = retired; stage = "terminal-fence"; }
    }
    int64_t next = esp_timer_get_time() + 100000;
    portENTER_CRITICAL(&s_action_retired_lock);
    s_action_retired.token = token;
    s_action_retired.active = token.identity != 0U;
    s_action_retired.error = error;
    s_action_retired.stage = stage;
    s_action_retired.next_retry_us = next;
    s_action_retired.busy = false;
    portEXIT_CRITICAL(&s_action_retired_lock);
}

static bool action_service(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, void *opaque)
{
    (void)ctx; (void)runtime; (void)opaque;
    bool roc_handled = esp32_mquickjs_wifi_roc_service();
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_action_retired_lock);
    bool submit = s_action_retired.active && !s_action_retired.busy && now >= s_action_retired.next_retry_us;
    if (submit) s_action_retired.busy = true;
    portEXIT_CRITICAL(&s_action_retired_lock);
    if (!submit) return roc_handled;
    if (esp32_mquickjs_submit_background_worker(action_cleanup_worker, NULL)) return true;
    portENTER_CRITICAL(&s_action_retired_lock);
    s_action_retired.busy = false;
    s_action_retired.next_retry_us = now + 100000;
    portEXIT_CRITICAL(&s_action_retired_lock);
    return roc_handled;
}

bool esp32_mquickjs_prepare_wifi_action_runtime_destroy(void)
{
    bool roc_drained = esp32_mquickjs_wifi_roc_prepare_runtime_destroy();
    (void)action_service(NULL, NULL, NULL);
    return !action_retired_pending() && roc_drained;
}

static JSValue action_error(JSContext *ctx, const char *code, esp_err_t error, const char *stage)
{
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espCode", JS_NewInt32(ctx, error)) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "espName", JS_NewString(ctx, esp_err_to_name(error))) ||
        !esp32_mquickjs_set_property_ref(ctx, details, "stage", stage ? JS_NewString(ctx, stage) : JS_NULL)) {
        JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
    }
    JSValue result = esp32_mquickjs_throw_native_error(ctx, code, "wifi.action.send",
        "Action send did not complete; inspect wifi.action.status() for native cleanup", *details);
    JS_PopGCRef(ctx, &ref);
    return result;
}

static int action_hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool action_mac(JSContext *ctx, JSValue value, uint8_t output[6])
{
    if (!JS_IsString(ctx, value)) return false;
    size_t length = 0;
    JSCStringBuf buffer;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (!text) return false;
    bool ok = length == 17;
    for (size_t i = 0; ok && i < 6; ++i) {
        int high = action_hex(text[i * 3]), low = action_hex(text[i * 3 + 1]);
        ok = high >= 0 && low >= 0 && (i == 5 || text[i * 3 + 2] == ':');
        if (ok) output[i] = (uint8_t)(high * 16 + low);
    }
    uint8_t any = 0;
    for (size_t i = 0; i < 6; ++i) any |= output[i];
    return ok && any != 0;
}

static bool action_capture_options(JSContext *ctx, JSGCRef *root, action_options_t *output)
{
    static const char *const keys[] = {"interface", "channel", "secondaryChannel", "destination", "bssid", "payload", "waitMs", "timeoutMs", "noAck"};
    static const char *const interfaces[] = {"station", "access-point"};
    static const char *const secondary[] = {"none", "above", "below"};
    action_options_t options = {.request = {.ifx = WIFI_IF_STA, .type = WIFI_OFFCHAN_TX_REQ,
        .wait_time_ms = 100, .rx_cb = esp32_mquickjs_wifi_action_receive}, .timeout_ms = 1000};
    JSGCRef ref;
    JSValue *field = JS_PushGCRef(ctx, &ref);
    bool ok = false;
    size_t choice;
    uint32_t number;
    if (!esp32_mquickjs_validate_plain_options(ctx, root->val, "wifi.action.send", keys, 9)) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, root->val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("interface");
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *field, interfaces, 2, &choice)) goto invalid;
        options.request.ifx = choice == 0 ? WIFI_IF_STA : WIFI_IF_AP;
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
    options.request.channel = number;
    FIELD("secondaryChannel");
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_enum(ctx, *field, secondary, 3, &choice)) goto invalid;
        options.request.sec_channel = choice == 0 ? WIFI_SECOND_CHAN_NONE : choice == 1 ? WIFI_SECOND_CHAN_ABOVE : WIFI_SECOND_CHAN_BELOW;
    }
    FIELD("destination");
    if (!action_mac(ctx, *field, options.request.dest_mac)) goto invalid;
    FIELD("bssid");
    if (!JS_IsUndefined(*field) && !action_mac(ctx, *field, options.request.bssid)) goto invalid;
    FIELD("waitMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 60000, &options.request.wait_time_ms)) goto invalid;
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 60000, &options.timeout_ms)) goto invalid;
    FIELD("noAck");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        options.request.no_ack = *field == JS_TRUE;
    }
    *output = options;
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid wifi.action.send options");
done:
    JS_PopGCRef(ctx, &ref);
    return ok;
#undef FIELD
}

/* Capture directly into the native SDK request. All JS getters run before
 * admission; a ByteView read lease stays rooted until its copy is finished. */
static bool action_capture_payload(JSContext *ctx, JSGCRef *value,
    const action_options_t *options, esp32_mquickjs_future_driver_state_t *state)
{
    JSGCRef ref;
    JSValue *field = JS_PushGCRef(ctx, &ref);
    const uint8_t *bytes = NULL;
    size_t length = 0;
    bool leased = false, ok = false;
    if (JS_GetClassID(ctx, value->val) == JS_CLASS_BYTE_VIEW) {
        if (!esp32_mquickjs_byte_view_acquire_read(ctx, value->val, "wifi.action.send", &bytes, &length)) goto done;
        leased = true;
    } else {
        if (JS_GetClassID(ctx, value->val) < 0) goto invalid;
        *field = JS_GetPropertyStr(ctx, value->val, "length");
        if (JS_IsException(*field)) goto done;
        uint32_t count;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 1476, &count)) goto invalid;
        length = count;
    }
    if (length < 1 || length > 1476 || length > SIZE_MAX - sizeof(*state->request)) goto invalid;
    state->request_bytes = sizeof(*state->request) + length;
    state->request = esp32_mquickjs_memory_wireless_alloc("wifi.action", state->request_bytes, ESP32_MQUICKJS_MEMORY_EXTERNAL, ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    if (!state->request) { JS_ThrowOutOfMemory(ctx); goto done; }
    *state->request = options->request;
    state->request->data_len = length;
    if (leased) memcpy(state->request->data, bytes, length);
    else for (uint32_t i = 0; i < length; ++i) {
        *field = JS_GetPropertyUint32(ctx, value->val, i);
        if (JS_IsException(*field)) goto done;
        uint32_t byte;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0, 255, &byte)) goto invalid;
        state->request->data[i] = byte;
    }
    ok = true;
    goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "wifi.action.send payload must contain 1-1476 bytes");
done:
    if (leased) esp32_mquickjs_byte_view_release_read(ctx, value->val);
    JS_PopGCRef(ctx, &ref);
    return ok;
}

static bool action_capture(JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    (void)this_ref;
    *output = NULL;
    if (argc != 1) { JS_ThrowTypeError(ctx, "wifi.action.send expects options"); return false; }
    action_options_t options;
    if (!action_capture_options(ctx, &argv[0], &options)) return false;
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { JS_ThrowOutOfMemory(ctx); return false; }
    state->timeout_ms = options.timeout_ms;
    atomic_init(&state->worker_done, false);
    atomic_init(&state->cancel_requested, false);
    JSGCRef ref;
    JSValue *payload = JS_PushGCRef(ctx, &ref);
    *payload = JS_GetPropertyStr(ctx, argv[0].val, "payload");
    bool ok = !JS_IsException(*payload) && action_capture_payload(ctx, &ref, &options, state);
    JS_PopGCRef(ctx, &ref);
    if (!ok) {
        esp32_mquickjs_memory_payload_free(state->request);
        esp32_mquickjs_memory_payload_free(state);
        return false;
    }
    *output = state;
    return true;
}

static void action_send_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    if (!atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) {
        state->stage = "submit";
        state->error = esp32_mquickjs_wifi_radio_action_send(state->request, state->request_bytes, &state->token);
    }
    /* Final access: worker items have no runtime/task wake pointer. */
    atomic_store_explicit(&state->worker_done, true, memory_order_release);
}

static void action_schedule(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->submitted || atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return;
    (void)action_service(NULL, NULL, NULL);
    if (!action_retired_pending()) state->submitted = esp32_mquickjs_submit_background_worker(action_send_worker, state);
}
static bool action_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    action_schedule(state);
    return true;
}
static esp32_mquickjs_future_poll_t action_poll(esp32_mquickjs_future_driver_state_t *state)
{
    action_schedule(state);
    if (state->submitted && !atomic_load_explicit(&state->worker_done, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (atomic_load_explicit(&state->cancel_requested, memory_order_acquire)) return ESP32_MQUICKJS_FUTURE_READY;
    if (!state->submitted) return ESP32_MQUICKJS_FUTURE_PENDING;
    if (state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    if (!esp32_mquickjs_wifi_radio_action_status(&state->token, &state->result)) {
        state->error = ESP_ERR_INVALID_STATE;
        state->stage = "completion-identity";
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->result.physical_termination) {
        state->error = ESP_ERR_INVALID_STATE;
        state->stage = "native-terminated";
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (state->result.ambiguous) {
        state->error = ESP_ERR_INVALID_STATE;
        state->stage = "completion-identity";
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    return state->result.terminal ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)
static JSValue action_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->error != ESP_OK) return action_error(ctx, "WIFI_ACTION_SEND_FAILED", state->error, state->stage);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, state->token.identity));
    SET(result, "radioGeneration", JS_NewUint32(ctx, state->token.generation));
    SET(result, "operationId", JS_NewUint32(ctx, state->result.operation_id));
    SET(result, "interface", JS_NewString(ctx, state->request->ifx == WIFI_IF_STA ? "station" : "access-point"));
    SET(result, "channel", JS_NewUint32(ctx, state->request->channel));
    SET(result, "payloadBytes", JS_NewUint32(ctx, state->request->data_len));
    SET(result, "driverStatus", JS_NewString(ctx, state->result.tx_status == WIFI_ACTION_TX_DONE ? "success" : state->result.tx_status == WIFI_ACTION_TX_FAILED ? "failed" : "unknown"));
    SET(result, "terminalStatus", JS_NewString(ctx, state->result.terminal_status == WIFI_ACTION_TX_DURATION_COMPLETED ? "duration-completed" : "cancelled"));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static esp32_mquickjs_cancel_result_t action_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static void action_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state) return;
    /* Future waits for worker_done before this call, including timeout/teardown.
     * SDK copied request storage; only the native token survives public finish. */
    if (state->token.identity != 0U) {
        portENTER_CRITICAL(&s_action_retired_lock);
        s_action_retired.token = state->token;
        s_action_retired.cancel = state->error != ESP_OK || atomic_load_explicit(&state->cancel_requested, memory_order_acquire);
        s_action_retired.active = true;
        s_action_retired.error = ESP_OK;
        s_action_retired.stage = "terminal-fence";
        s_action_retired.next_retry_us = 0;
        portEXIT_CRITICAL(&s_action_retired_lock);
    }
    esp32_mquickjs_memory_payload_free(state->request);
    esp32_mquickjs_memory_payload_free(state);
    (void)action_service(NULL, NULL, NULL);
}
static uint32_t action_timeout(const esp32_mquickjs_future_driver_state_t *state) { return state->timeout_ms; }
static JSValue action_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    atomic_store_explicit(&state->cancel_requested, true, memory_order_release);
    return action_error(ctx, "WIFI_ACTION_TIMEOUT", ESP_ERR_TIMEOUT, "deadline");
}
static esp32_mquickjs_resource_key_t action_resource(const esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return &s_action_future_lane;
}
static const esp32_mquickjs_future_driver_t s_action_driver = {
    .memory_owner = "wireless.future", .capture = action_capture, .start = action_start, .poll = action_poll, .finish = action_finish,
    .cancel = action_cancel, .destroy = action_destroy, .timeout_ms = action_timeout,
    .on_timeout = action_on_timeout, .resource_key = action_resource,
};

JSValue esp32_mquickjs_wifi_action_status(JSContext *ctx)
{
    esp32_mquickjs_wifi_action_lane_t native;
    esp32_mquickjs_wifi_radio_action_snapshot(&native);
    esp32_mquickjs_wifi_roc_status_t roc_status = {0};
    (void)esp32_mquickjs_wifi_roc_current_status(&roc_status);
    uint32_t roc_handles;
    bool roc_active, roc_cleanup;
    esp32_mquickjs_wifi_roc_counts(&roc_handles, &roc_active, &roc_cleanup);
    portENTER_CRITICAL(&s_action_retired_lock);
    bool pending = s_action_retired.active;
    esp_err_t error = s_action_retired.error;
    const char *stage = s_action_retired.stage;
    portEXIT_CRITICAL(&s_action_retired_lock);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "operationActive", JS_NewBool(native.identity != 0U));
    SET(result, "kind", native.identity ? JS_NewString(ctx, native.kind == ESP32_MQUICKJS_WIFI_ACTION_ROC ? "roc" : "send") : JS_NULL);
    SET(result, "rocHandles", JS_NewUint32(ctx, roc_handles));
    SET(result, "rocActive", JS_NewBool(roc_active));
    SET(result, "rocError", roc_status.error ? JS_NewInt32(ctx, roc_status.error) : JS_NULL);
    SET(result, "rocStage", roc_status.stage ? JS_NewString(ctx, roc_status.stage) : JS_NULL);
    SET(result, "rocCleanupError", roc_status.cleanup_error ? JS_NewInt32(ctx, roc_status.cleanup_error) : JS_NULL);
    SET(result, "rocCleanupStage", roc_status.cleanup_stage ? JS_NewString(ctx, roc_status.cleanup_stage) : JS_NULL);
    SET(result, "sequence", native.identity ? JS_NewUint32(ctx, native.identity) : JS_NULL);
    SET(result, "radioGeneration", native.identity ? JS_NewUint32(ctx, native.generation) : JS_NULL);
    SET(result, "operationId", native.submitted ? JS_NewUint32(ctx, native.operation_id) : JS_NULL);
    SET(result, "terminal", JS_NewBool(native.terminal));
    SET(result, "ambiguous", JS_NewBool(native.ambiguous));
    SET(result, "nativeQuiescent", JS_NewBool(native.sdk_quiescent));
    SET(result, "nativeTerminated", JS_NewBool(native.physical_termination));
    SET(result, "identityExhausted", JS_NewBool(native.next_identity == 0U));
    SET(result, "cancelWritten", JS_NewBool(native.cancel_written));
    SET(result, "sdkFenced", JS_NewBool(native.sdk_fenced));
    SET(result, "eventFenced", JS_NewBool(native.event_fenced));
    SET(result, "cleanupPending", JS_NewBool(pending || roc_cleanup));
    SET(result, "cleanupError", error ? JS_NewInt32(ctx, error) : JS_NULL);
    SET(result, "cleanupStage", stage ? JS_NewString(ctx, stage) : JS_NULL);
    SET(result, "submitError", native.submit_error ? JS_NewInt32(ctx, native.submit_error) : JS_NULL);
    SET(result, "cancelError", native.cancel_error ? JS_NewInt32(ctx, native.cancel_error) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_action_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.action.status expects no arguments");
    return esp32_mquickjs_wifi_action_status(ctx);
}
JSValue js_wifi_action_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.action.capabilities expects no arguments");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-action/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    SET(result, "accessPoint", JS_TRUE);
#else
    SET(result, "accessPoint", JS_FALSE);
#endif
    SET(result, "maximumPayloadBytes", JS_NewInt32(ctx, 1476));
    SET(result, "maximumOperations", JS_NewInt32(ctx, 1));
    SET(result, "maximumRocHandles", JS_NewInt32(ctx, ESP32_MQUICKJS_WIFI_ROC_MAX_HANDLES));
    SET(result, "remainOnChannel", JS_TRUE);
    SET(result, "recover", JS_TRUE);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

JSValue js_wifi_action_send(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "action");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "send");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *module, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return result;
}

bool esp32_mquickjs_init_wifi_action_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, wifi_ref, module_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "action");
    *method = JS_IsException(*module) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *module, "send");
    bool ok = !JS_IsException(*method) && esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_action_driver) &&
        esp32_mquickjs_register_async_poller(runtime, action_service, NULL) &&
        esp32_mquickjs_init_wifi_roc_runtime(ctx, runtime) &&
        esp32_mquickjs_init_wifi_action_recovery_runtime(ctx, runtime);
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register Action runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &module_ref);
    JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
