#include "esp32_mquickjs_wifi_ftm.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
#include "esp32_mquickjs_wifi_ftm_session.h"
#include "esp32_mquickjs_wifi_radio.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdio.h>

typedef enum { FTM_OPEN, FTM_RECEIVE, FTM_END, FTM_CLOSE } ftm_operation_t;
typedef struct {
    wifi_ftm_initiator_cfg_t config;
    uint32_t timeout_ms, report_capacity;
} ftm_options_t;
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_ftm_session_t *session;
    uint32_t timeout_ms;
    ftm_operation_t operation;
    esp_err_t error;
    int64_t receive_deadline_us;
    bool started, cancelled, receive_timed_out;
};
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

static int ftm_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool ftm_mac(JSContext *ctx, JSValue value, uint8_t output[6])
{
    if (!JS_IsString(ctx, value)) return false;
    size_t length = 0;
    JSCStringBuf buffer;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (text == NULL || length != 17) return false;
    for (size_t i = 0; i < 6; ++i) {
        int high = ftm_hex(text[i * 3]), low = ftm_hex(text[i * 3 + 1]);
        if (high < 0 || low < 0 || (i != 5 && text[i * 3 + 2] != ':')) return false;
        output[i] = (uint8_t)(high * 16 + low);
    }
    return true;
}
static JSValue ftm_address(JSContext *ctx, const uint8_t bytes[6])
{
    char address[18];
    snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return JS_NewString(ctx, address);
}
static bool ftm_options(JSContext *ctx, JSGCRef *root, ftm_options_t *output)
{
    static const char *const keys[] = {"peerAddress", "channel", "frameCount", "burstPeriodMs", "maxReportEntries", "timeoutMs"};
    ftm_options_t options = {.timeout_ms = 1000, .report_capacity = ESP32_MQUICKJS_WIFI_FTM_MAX_REPORT_ENTRIES};
    uint32_t number;
    JSGCRef ref;
    JSValue *field = JS_PushGCRef(ctx, &ref);
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, root->val, "wifi.ftm.start", keys, 6)) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, root->val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("peerAddress");
    if (!ftm_mac(ctx, *field, options.config.resp_mac)) goto invalid;
    FIELD("channel");
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 177, &number)) goto invalid;
    if (number > 14) {
#if CONFIG_SOC_WIFI_SUPPORT_5G
        if (!esp32_mquickjs_wifi_radio_5ghz_channel_bit(number)) goto invalid;
#else
        goto invalid;
#endif
    }
    options.config.channel = (uint8_t)number;
    FIELD("frameCount");
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0, 64, &number)) goto invalid;
        options.config.frm_count = (uint8_t)number;
    }
    FIELD("burstPeriodMs");
    if (!JS_IsUndefined(*field)) {
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0, 10000, &number) || number % 100 != 0) goto invalid;
        options.config.burst_period = (uint16_t)(number / 100);
    }
    FIELD("maxReportEntries");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 0,
        ESP32_MQUICKJS_WIFI_FTM_MAX_REPORT_ENTRIES, &options.report_capacity)) goto invalid;
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 60000, &options.timeout_ms)) goto invalid;
    if (!esp32_mquickjs_wifi_ftm_config_valid(&options.config)) goto invalid;
    *output = options; ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid FTM options");
done:
    JS_PopGCRef(ctx, &ref); return ok;
#undef FIELD
}

static esp32_mquickjs_wifi_ftm_session_t *ftm_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_FTM_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiFtmSession"); return NULL;
    }
    esp32_mquickjs_wifi_ftm_session_t *session = JS_GetOpaque(ctx, value);
    if (session == NULL) JS_ThrowReferenceError(ctx, "invalid WiFiFtmSession");
    return session;
}
void js_wifi_ftm_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    esp32_mquickjs_wifi_ftm_close(opaque);
    esp32_mquickjs_wifi_ftm_release(opaque);
}
JSValue js_wifi_ftm_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.ftm.start()");
}
static const char *ftm_status_name(wifi_ftm_status_t status)
{
    static const char *const names[] = {"success", "unsupported", "configuration-rejected", "no-response", "failed", "no-valid-measurement", "terminated"};
    return (unsigned)status <= FTM_STATUS_USER_TERM ? names[(unsigned)status] : "unknown";
}
static const char *ftm_operation_name(ftm_operation_t operation)
{
    return operation == FTM_OPEN ? "wifi.ftm.start" : operation == FTM_RECEIVE ? "WiFiFtmSession.receive" :
        operation == FTM_END ? "WiFiFtmSession.end" : "WiFiFtmSession.close";
}
static JSValue ftm_error(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, bool timeout)
{
    esp32_mquickjs_wifi_ftm_status_t status = {0};
    if (state->session != NULL) (void)esp32_mquickjs_wifi_ftm_status(state->session, &status);
    bool closed = state->operation == FTM_RECEIVE && status.close_requested;
    esp_err_t error = timeout ? ESP_ERR_TIMEOUT : state->error ? state->error : status.error ? status.error : ESP_ERR_INVALID_STATE;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "stage", JS_NewString(ctx, timeout ? "deadline" : closed ? "closed" : status.stage ? status.stage : "ftm-state"));
    SET(details, "sequence", status.native.token.identity ? JS_NewUint32(ctx, status.native.token.identity) : JS_NULL);
    SET(details, "radioGeneration", status.native.token.identity ? JS_NewUint32(ctx, status.native.token.generation) : JS_NULL);
    SET(details, "endRequested", JS_NewBool(status.end_requested));
    SET(details, "closeRequested", JS_NewBool(status.close_requested));
    SET(details, "cleanupPending", JS_NewBool(status.started && !status.retired));
    SET(details, "cleanupError", status.cleanup_error ? JS_NewInt32(ctx, status.cleanup_error) : JS_NULL);
    SET(details, "cleanupStage", status.cleanup_stage ? JS_NewString(ctx, status.cleanup_stage) : JS_NULL);
    (void)esp32_mquickjs_throw_native_error(ctx, timeout ? "WIFI_FTM_TIMEOUT" : closed ? "WIFI_FTM_CLOSED" : "WIFI_FTM_FAILED",
        ftm_operation_name(state->operation), "FTM operation did not complete; inspect session or wifi.ftm.status()", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue ftm_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_ftm_status_t *status)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    bool fault = status->error || status->native.ambiguous ||
        (status->cleanup_error != ESP_OK && status->cleanup_error != ESP_ERR_TIMEOUT);
    SET(result, "state", JS_NewString(ctx, status->close_requested ? status->retired ? "closed" : "closing" :
        fault ? "faulted" : status->retired ? "completed" : status->native.terminal ? "draining" :
        status->end_requested ? "ending" : status->submitted ? "active" : "opening"));
    SET(result, "peerAddress", ftm_address(ctx, status->config.resp_mac));
    SET(result, "channel", JS_NewUint32(ctx, status->config.channel));
    SET(result, "frameCount", JS_NewUint32(ctx, status->config.frm_count));
    SET(result, "burstPeriodMs", JS_NewUint32(ctx, status->config.burst_period * 100U));
    SET(result, "driverAccepted", JS_NewBool(status->native.submitted && status->native.submit_error == ESP_OK));
    SET(result, "endRequested", JS_NewBool(status->end_requested));
    SET(result, "closeRequested", JS_NewBool(status->close_requested));
    SET(result, "cleanupPending", JS_NewBool(status->started && !status->retired));
    SET(result, "sequence", status->native.token.identity ? JS_NewUint32(ctx, status->native.token.identity) : JS_NULL);
    SET(result, "radioGeneration", status->native.token.identity ? JS_NewUint32(ctx, status->native.token.generation) : JS_NULL);
    SET(result, "terminalStatus", status->native.terminal ? JS_NewString(ctx, ftm_status_name(status->native.report.status)) : JS_NULL);
    SET(result, "terminalStatusId", status->native.terminal ? JS_NewInt32(ctx, status->native.report.status) : JS_NULL);
    SET(result, "ambiguous", JS_NewBool(status->native.ambiguous));
    SET(result, "physicalTermination", JS_NewBool(status->native.physical_termination));
    SET(result, "reportReady", JS_NewBool(status->report_ready));
    SET(result, "reportConsumed", JS_NewBool(status->native.report_consumed));
    SET(result, "reportDiscarded", JS_NewBool(status->native.report_discarded));
    SET(result, "reportCapacity", JS_NewUint32(ctx, status->report_capacity));
    SET(result, "retainedEntries", JS_NewUint32(ctx, status->retained_entries));
    SET(result, "reportEntries", status->native.terminal ? JS_NewUint32(ctx, status->native.report.ftm_report_num_entries) : JS_NULL);
    SET(result, "copiedEntries", JS_NewUint32(ctx, status->native.copied_entries));
    SET(result, "sdkFenced", JS_NewBool(status->native.sdk_fenced));
    SET(result, "eventFenced", JS_NewBool(status->native.event_fenced));
    SET(result, "error", status->error ? JS_NewInt32(ctx, status->error) : JS_NULL);
    SET(result, "stage", status->stage ? JS_NewString(ctx, status->stage) : JS_NULL);
    SET(result, "cleanupError", status->cleanup_error ? JS_NewInt32(ctx, status->cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", status->cleanup_stage ? JS_NewString(ctx, status->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue ftm_timestamp(JSContext *ctx, uint64_t value)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "low", JS_NewUint32(ctx, (uint32_t)value));
    SET(result, "high", JS_NewUint32(ctx, (uint32_t)(value >> 32)));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue ftm_report_to_js(JSContext *ctx, esp32_mquickjs_wifi_ftm_session_t *session,
    const esp32_mquickjs_wifi_ftm_status_t *status)
{
    JSGCRef result_ref, entries_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref), *entries = JS_PushGCRef(ctx, &entries_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    *entries = JS_NewArray(ctx, 0);
    if (JS_IsException(*result) || JS_IsException(*entries)) goto fail;
    bool measured = status->native.report.status == FTM_STATUS_SUCCESS && status->native.report.ftm_report_num_entries != 0U;
    SET(result, "sequence", JS_NewUint32(ctx, status->native.token.identity));
    SET(result, "radioGeneration", JS_NewUint32(ctx, status->native.token.generation));
    SET(result, "peerAddress", ftm_address(ctx, status->native.report.peer_mac));
    SET(result, "status", JS_NewString(ctx, ftm_status_name(status->native.report.status)));
    SET(result, "statusId", JS_NewInt32(ctx, status->native.report.status));
    SET(result, "rttRawNs", measured ? JS_NewUint32(ctx, status->native.report.rtt_raw) : JS_NULL);
    SET(result, "rttEstimatedNs", measured ? JS_NewUint32(ctx, status->native.report.rtt_est) : JS_NULL);
    SET(result, "distanceCm", measured ? JS_NewUint32(ctx, status->native.report.dist_est) : JS_NULL);
    SET(result, "reportEntries", JS_NewUint32(ctx, status->native.report.ftm_report_num_entries));
    SET(result, "copiedEntries", JS_NewUint32(ctx, status->native.copied_entries));
    SET(result, "truncated", JS_NewBool(status->native.copied_entries < status->native.report.ftm_report_num_entries));
    for (unsigned i = 0; i < status->native.copied_entries; ++i) {
        wifi_ftm_report_entry_t entry;
        if (!esp32_mquickjs_wifi_ftm_report_entry(session, i, &entry)) {
            JS_ThrowReferenceError(ctx, "FTM report was closed during conversion"); goto fail;
        }
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item)) goto fail;
        SET(item, "dialogToken", JS_NewUint32(ctx, entry.dlog_token));
        SET(item, "rssi", JS_NewInt32(ctx, entry.rssi));
        SET(item, "rttPs", JS_NewUint32(ctx, entry.rtt));
        SET(item, "t1Ps", ftm_timestamp(ctx, entry.t1));
        SET(item, "t2Ps", ftm_timestamp(ctx, entry.t2));
        SET(item, "t3Ps", ftm_timestamp(ctx, entry.t3));
        SET(item, "t4Ps", ftm_timestamp(ctx, entry.t4));
        SET(item, "ppm", JS_NewInt32(ctx, entry.ppm));
        if (JS_IsException(JS_SetPropertyUint32(ctx, *entries, i, *item))) goto fail;
    }
    SET(result, "entries", *entries);
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &entries_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &entries_ref); JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}
JSValue js_wifi_ftm_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "WiFiFtmSession.status expects no arguments");
    esp32_mquickjs_wifi_ftm_session_t *session = ftm_receiver(ctx, *self);
    if (session == NULL) return JS_EXCEPTION;
    esp32_mquickjs_wifi_ftm_status_t status;
    (void)esp32_mquickjs_wifi_ftm_status(session, &status);
    return ftm_status_to_js(ctx, &status);
}
JSValue js_wifi_ftm_global_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "wifi.ftm.status expects no arguments");
    esp32_mquickjs_wifi_ftm_counts_t counts;
    esp32_mquickjs_wifi_ftm_counts(&counts);
    esp32_mquickjs_wifi_ftm_status_t status;
    bool active = esp32_mquickjs_wifi_ftm_current_status(&status);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "active", JS_NewBool(counts.active));
    SET(result, "handles", JS_NewUint32(ctx, counts.handles));
    SET(result, "reservedEntries", JS_NewUint32(ctx, counts.reserved_entries));
    SET(result, "workerBusy", JS_NewBool(counts.worker_busy));
    SET(result, "cleanupPending", JS_NewBool(counts.cleanup_pending));
    SET(result, "session", active ? ftm_status_to_js(ctx, &status) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static void ftm_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    bool service = state->started && state->operation != FTM_RECEIVE;
    if (state->session != NULL) {
        if (state->operation == FTM_OPEN) esp32_mquickjs_wifi_ftm_close(state->session);
        esp32_mquickjs_wifi_ftm_release(state->session);
    }
    esp32_mquickjs_memory_payload_free(state);
    if (service) (void)esp32_mquickjs_wifi_ftm_service();
}
static bool ftm_open_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output)
{
    (void)self; *output = NULL;
    if (argc != 1) { JS_ThrowTypeError(ctx, "wifi.ftm.start expects options"); return false; }
    ftm_options_t options;
    if (!ftm_options(ctx, &argv[0], &options)) return false;
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { JS_ThrowOutOfMemory(ctx); return false; }
    state->operation = FTM_OPEN; state->timeout_ms = options.timeout_ms;
    state->error = esp32_mquickjs_wifi_ftm_create(&options.config, options.report_capacity, &state->session);
    if (state->error != ESP_OK) { (void)ftm_error(ctx, state, false); ftm_destroy(state); return false; }
    *output = state; return true;
}
static bool ftm_method_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, ftm_operation_t operation)
{
    *output = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "%s expects optional timeout options", ftm_operation_name(operation)); return false; }
    esp32_mquickjs_wifi_ftm_session_t *session = ftm_receiver(ctx, self->val);
    if (session == NULL) return false;
    /* Retain before options getters can close the receiver or trigger GC. */
    if (!esp32_mquickjs_wifi_ftm_retain(session)) { JS_ThrowInternalError(ctx, "FTM reference exhausted"); return false; }
    uint32_t timeout = 1000;
    bool valid = true;
    if (argc > 0 && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        valid = esp32_mquickjs_validate_plain_options(ctx, argv[0].val, ftm_operation_name(operation), keys, 1);
        if (valid) {
            JSGCRef ref;
            JSValue *value = JS_PushGCRef(ctx, &ref);
            *value = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
            valid = !JS_IsException(*value) && (JS_IsUndefined(*value) || esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 60000, &timeout));
            JS_PopGCRef(ctx, &ref);
        }
    }
    if (!valid) {
        esp32_mquickjs_wifi_ftm_release(session);
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid FTM timeout options");
        return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { esp32_mquickjs_wifi_ftm_release(session); JS_ThrowOutOfMemory(ctx); return false; }
    state->session = session; state->operation = operation; state->timeout_ms = timeout;
    *output = state; return true;
}
static bool ftm_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **output)
{ return ftm_method_capture(ctx, self, argc, argv, output, FTM_RECEIVE); }
static bool ftm_end_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **output)
{ return ftm_method_capture(ctx, self, argc, argv, output, FTM_END); }
static bool ftm_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **output)
{ return ftm_method_capture(ctx, self, argc, argv, output, FTM_CLOSE); }
static bool ftm_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    state->started = true;
    if (state->operation == FTM_RECEIVE) state->receive_deadline_us = esp_timer_get_time() + (int64_t)state->timeout_ms * 1000;
    if (state->operation == FTM_OPEN) state->error = esp32_mquickjs_wifi_ftm_start(state->session);
    if (state->operation == FTM_END) esp32_mquickjs_wifi_ftm_end(state->session);
    if (state->operation == FTM_CLOSE) esp32_mquickjs_wifi_ftm_close(state->session);
    (void)esp32_mquickjs_wifi_ftm_service();
    return true;
}
static esp32_mquickjs_future_poll_t ftm_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled || state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    (void)esp32_mquickjs_wifi_ftm_service();
    esp32_mquickjs_wifi_ftm_status_t status;
    (void)esp32_mquickjs_wifi_ftm_status(state->session, &status);
    bool ready = state->operation == FTM_OPEN ? status.submitted || status.retired :
        state->operation == FTM_RECEIVE ? status.retired || status.close_requested || status.error != ESP_OK : status.retired;
    if (!ready && state->operation == FTM_RECEIVE && esp_timer_get_time() >= state->receive_deadline_us) {
        state->receive_timed_out = true;
        ready = true;
    }
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue ftm_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->operation == FTM_RECEIVE && state->receive_timed_out) return JS_NULL;
    esp32_mquickjs_wifi_ftm_status_t status;
    (void)esp32_mquickjs_wifi_ftm_status(state->session, &status);
    if (state->error != ESP_OK || (state->operation != FTM_CLOSE && status.error != ESP_OK)) return ftm_error(ctx, state, false);
    if (state->operation == FTM_OPEN) {
        JSValue result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_FTM_SESSION);
        if (JS_IsException(result)) return result;
        JS_SetOpaque(ctx, result, state->session);
        state->session = NULL;
        return result;
    }
    if (state->operation != FTM_RECEIVE) return JS_UNDEFINED;
    if (status.close_requested || !status.report_ready) return ftm_error(ctx, state, false);
    return ftm_report_to_js(ctx, state->session, &status);
}
static esp32_mquickjs_cancel_result_t ftm_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    /* receive changes no native intent. Started end preserves its report;
     * started close keeps cleanup; an abandoned open leaves no orphan handle. */
    if (state->started && (state->operation == FTM_OPEN || state->operation == FTM_CLOSE))
        esp32_mquickjs_wifi_ftm_close(state->session);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
/* Core on_timeout is a rejection hook, not a nullable completion. receive uses
 * its own scheduled-wait deadline and finishes normally with null. */
static uint32_t ftm_timeout(const esp32_mquickjs_future_driver_state_t *state)
{
    return state->operation == FTM_RECEIVE ? 0U : state->timeout_ms;
}
static JSValue ftm_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    (void)ftm_cancel(state);
    return ftm_error(ctx, state, true);
}
#define FTM_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = ftm_start, .poll = ftm_poll, .finish = ftm_finish, \
    .cancel = ftm_cancel, .destroy = ftm_destroy, .timeout_ms = ftm_timeout, .on_timeout = ftm_on_timeout }
static const esp32_mquickjs_future_driver_t s_ftm_open_driver = FTM_DRIVER(ftm_open_capture);
static const esp32_mquickjs_future_driver_t s_ftm_receive_driver = FTM_DRIVER(ftm_receive_capture);
static const esp32_mquickjs_future_driver_t s_ftm_end_driver = FTM_DRIVER(ftm_end_capture);
static const esp32_mquickjs_future_driver_t s_ftm_close_driver = FTM_DRIVER(ftm_close_capture);
static JSValue ftm_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver; *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref); return result;
}
JSValue js_wifi_ftm_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return ftm_call(ctx, self, argc, argv, "receive"); }
JSValue js_wifi_ftm_end(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return ftm_call(ctx, self, argc, argv, "end"); }
JSValue js_wifi_ftm_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return ftm_call(ctx, self, argc, argv, "close"); }
JSValue js_wifi_ftm_start(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "ftm");
    JSValue result = JS_IsException(*module) ? JS_EXCEPTION : ftm_call(ctx, module, argc, argv, "start");
    JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref); return result;
}
bool esp32_mquickjs_init_wifi_ftm_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, method_ref, child_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref), *child = JS_PushGCRef(ctx, &child_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "wifi");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "ftm");
    if (JS_IsException(*child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "start");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_ftm_open_driver)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiFtmSession");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "receive");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_ftm_receive_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "end");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_ftm_end_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_ftm_close_driver)) goto done;
    ok = esp32_mquickjs_init_wifi_ftm_recovery_runtime(ctx, runtime);
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register FTM runtime");
    JS_PopGCRef(ctx, &child_ref); JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
