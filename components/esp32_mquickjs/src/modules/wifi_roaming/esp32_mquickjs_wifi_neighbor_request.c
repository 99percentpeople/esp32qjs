#include "esp32_mquickjs_wifi_neighbor_request.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_RRM_SUPPORT
#include "esp32_mquickjs_wifi_rrm_request.h"
#include "esp32_mquickjs_wifi_neighbor.h"
#include "esp32_mquickjs_wifi.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_heap_caps.h"
#include <stdio.h>

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_rrm_request_t *request;
    JSContext *ctx;
    JSGCRef receiver;
    uint32_t timeout_ms;
    esp_err_t error;
    bool opening, started, transferred, cancelled, waiter, receiver_retained;
};
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)
static const char *neighbor_terminal(esp32_mquickjs_wifi_rrm_terminal_t value)
{
    static const char *const names[] = {"pending", "report", "no-report", "failed", "cancelled", "timed-out"};
    return (unsigned)value < sizeof(names) / sizeof(names[0]) ? names[value] : "unknown";
}
static esp32_mquickjs_wifi_rrm_request_t *neighbor_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_NEIGHBOR_REQUEST) {
        JS_ThrowTypeError(ctx, "expected WiFiNeighborReportRequest"); return NULL;
    }
    esp32_mquickjs_wifi_rrm_request_t *request = JS_GetOpaque(ctx, value);
    if (request == NULL) JS_ThrowReferenceError(ctx, "invalid WiFiNeighborReportRequest");
    return request;
}
static JSValue neighbor_snapshot(JSContext *ctx, const esp32_mquickjs_wifi_rrm_status_t *s)
{
    JSGCRef ref;
    JSValue *value = JS_PushGCRef(ctx, &ref);
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value)) goto fail;
    SET(value, "terminal", JS_NewString(ctx, neighbor_terminal(s->terminal)));
    SET(value, "started", JS_NewBool(s->started));
    SET(value, "closed", JS_NewBool(s->closed));
    SET(value, "retired", JS_NewBool(s->retired));
    SET(value, "cleanupPending", JS_NewBool(s->started && !s->retired));
    SET(value, "sequence", s->operation.identity ? JS_NewUint32(ctx, s->operation.identity) : JS_NULL);
    SET(value, "radioGeneration", s->operation.identity ? JS_NewUint32(ctx, s->operation.generation) : JS_NULL);
    SET(value, "nativeEntered", JS_NewBool(s->submit.entered));
    SET(value, "transmissionAttempted", JS_NewBool(s->submit.tx_attempted));
    SET(value, "sdkCode", s->submit.entered ? JS_NewInt32(ctx, s->submit.code) : JS_NULL);
    SET(value, "callbackSeen", JS_NewBool(s->callback_seen));
    SET(value, "cancelWritten", JS_NewBool(s->cancel_written));
    SET(value, "reportBytes", JS_NewUint32(ctx, s->received_bytes));
    SET(value, "reportCapacity", JS_NewUint32(ctx, s->capacity));
    SET(value, "retainedBytes", JS_NewUint32(ctx, s->retained_bytes));
    SET(value, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(value, "stage", s->stage ? JS_NewString(ctx, s->stage) : JS_NULL);
    SET(value, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(value, "cleanupStage", s->cleanup_stage ? JS_NewString(ctx, s->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue neighbor_error(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, const char *code, const char *stage)
{
    esp32_mquickjs_wifi_rrm_status_t status = {0};
    (void)esp32_mquickjs_wifi_rrm_status(state->request, &status);
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = neighbor_snapshot(ctx, &status);
    if (JS_IsException(*details)) goto fail;
    SET(details, "operationError", state->error ? JS_NewInt32(ctx, state->error) : JS_NULL);
    if (stage != NULL) SET(details, "stage", JS_NewString(ctx, stage));
    (void)esp32_mquickjs_throw_native_error(ctx, code,
        state->opening ? "wifi.roaming.requestNeighborReport" : "WiFiNeighborReportRequest.receive",
        "Neighbor Report operation did not complete; inspect native cleanup status", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_neighbor_module_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "wifi.roaming.status expects no arguments");
    esp32_mquickjs_wifi_rrm_counts_t counts;
    esp32_mquickjs_wifi_rrm_counts(&counts);
    esp32_mquickjs_wifi_rrm_status_t status;
    bool active = esp32_mquickjs_wifi_rrm_current_status(&status);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "handles", JS_NewUint32(ctx, counts.handles));
    SET(result, "reservedBytes", JS_NewUint32(ctx, counts.reserved_bytes));
    SET(result, "workerBusy", JS_NewBool(counts.worker_busy));
    SET(result, "callbackBusy", JS_NewBool(counts.callback_busy));
    SET(result, "activeRequest", active ? neighbor_snapshot(ctx, &status) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
void js_wifi_neighbor_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    esp32_mquickjs_wifi_rrm_close(opaque);
    esp32_mquickjs_wifi_rrm_release(opaque);
}
JSValue js_wifi_neighbor_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.roaming.requestNeighborReport()");
}
JSValue js_wifi_neighbor_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "status expects no arguments");
    esp32_mquickjs_wifi_rrm_request_t *request = neighbor_receiver(ctx, *self);
    if (request == NULL) return JS_EXCEPTION;
    esp32_mquickjs_wifi_rrm_status_t status;
    (void)esp32_mquickjs_wifi_rrm_status(request, &status);
    return neighbor_snapshot(ctx, &status);
}
JSValue js_wifi_neighbor_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "cancel expects no arguments");
    esp32_mquickjs_wifi_rrm_request_t *request = neighbor_receiver(ctx, *self);
    if (request == NULL) return JS_EXCEPTION;
    esp32_mquickjs_wifi_rrm_cancel(request, false);
    (void)esp32_mquickjs_wifi_rrm_service();
    return JS_UNDEFINED;
}
JSValue js_wifi_neighbor_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "close expects no arguments");
    esp32_mquickjs_wifi_rrm_request_t *request = neighbor_receiver(ctx, *self);
    if (request == NULL) return JS_EXCEPTION;
    esp32_mquickjs_wifi_rrm_close(request);
    (void)esp32_mquickjs_wifi_rrm_service();
    return JS_UNDEFINED;
}
static bool neighbor_options(JSContext *ctx, int argc, JSGCRef *argv, bool opening, uint32_t *timeout, uint32_t *capacity)
{
    static const char *const keys[] = {"timeoutMs", "maxReportBytes"};
    if (argc > 1) { JS_ThrowTypeError(ctx, "expected optional Neighbor Report options"); return false; }
    *timeout = 1500; *capacity = ESP32_MQUICKJS_WIFI_RRM_MAX_REPORT_BYTES;
    if (!argc || JS_IsUndefined(argv[0].val)) return true;
    if (!esp32_mquickjs_validate_plain_options(ctx, argv[0].val, "Neighbor Report", keys, opening ? 2 : 1)) return false;
    JSGCRef ref;
    JSValue *value = JS_PushGCRef(ctx, &ref);
    bool ok = false;
    *value = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
    if (JS_IsException(*value)) goto done;
    if (!JS_IsUndefined(*value) && !esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1, 60000, timeout)) goto invalid;
    if (opening) {
        *value = JS_GetPropertyStr(ctx, argv[0].val, "maxReportBytes");
        if (JS_IsException(*value)) goto done;
        if (!JS_IsUndefined(*value) && !esp32_mquickjs_value_to_bounded_u32(ctx, *value, 1,
            ESP32_MQUICKJS_WIFI_RRM_MAX_REPORT_BYTES, capacity)) goto invalid;
    }
    ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid Neighbor Report options");
done:
    JS_PopGCRef(ctx, &ref); return ok;
}
static void neighbor_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) return;
    if (state->waiter) esp32_mquickjs_wifi_rrm_waiter_remove(state->request);
    if (state->opening && !state->transferred) esp32_mquickjs_wifi_rrm_close(state->request);
    esp32_mquickjs_wifi_rrm_release(state->request);
    if (state->receiver_retained) JS_DeleteGCRef(state->ctx, &state->receiver);
    esp32_mquickjs_memory_payload_free(state);
    /* No observation here: Future core settles only after destroy returns. */
    (void)esp32_mquickjs_wifi_rrm_service();
}
static bool neighbor_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **output, bool opening)
{
    *output = NULL;
    esp32_mquickjs_wifi_rrm_request_t *request = NULL;
    if (!opening) {
        request = neighbor_receiver(ctx, self->val);
        if (request == NULL) return false;
        if (!esp32_mquickjs_wifi_rrm_retain(request)) { JS_ThrowInternalError(ctx, "request references exhausted"); return false; }
    }
    uint32_t timeout, capacity;
    if (!neighbor_options(ctx, argc, argv, opening, &timeout, &capacity)) {
        esp32_mquickjs_wifi_rrm_release(request); return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (state == NULL) { esp32_mquickjs_wifi_rrm_release(request); JS_ThrowOutOfMemory(ctx); return false; }
    state->opening = opening; state->timeout_ms = timeout; state->request = request;
    if (opening) state->error = esp32_mquickjs_wifi_rrm_create(capacity, &state->request);
    if (state->error != ESP_OK) { (void)neighbor_error(ctx, state, "WIFI_NEIGHBOR_FAILED", "request-allocation"); neighbor_destroy(state); return false; }
    state->waiter = esp32_mquickjs_wifi_rrm_waiter_add(state->request);
    if (!state->waiter) { neighbor_destroy(state); JS_ThrowInternalError(ctx, "request waiters exhausted"); return false; }
    if (!opening) {
        /* Core drops capture-time call roots. Keep the logical handle alive so
         * its finalizer cannot close a still-running receive Future. This state
         * stays on the runtime task; workers/callbacks retain only request. */
        state->ctx = ctx;
        *JS_AddGCRef(ctx, &state->receiver) = self->val;
        state->receiver_retained = true;
    }
    *output = state; return true;
}
static bool neighbor_open_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **output)
{ return neighbor_capture(ctx, self, argc, argv, output, true); }
static bool neighbor_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **output)
{ return neighbor_capture(ctx, self, argc, argv, output, false); }
static bool neighbor_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, esp32_mquickjs_future_token_t token,
    esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    state->started = true;
    if (state->opening) state->error = esp32_mquickjs_wifi_rrm_start(state->request);
    (void)esp32_mquickjs_wifi_rrm_service();
    return true;
}
static esp32_mquickjs_future_poll_t neighbor_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled || state->error != ESP_OK) return ESP32_MQUICKJS_FUTURE_READY;
    (void)esp32_mquickjs_wifi_rrm_service();
    esp32_mquickjs_wifi_rrm_status_t s;
    (void)esp32_mquickjs_wifi_rrm_status(state->request, &s);
    bool ready = state->opening ? s.submit.entered || s.retired :
        s.retired || s.closed || (s.terminal != ESP32_MQUICKJS_WIFI_RRM_PENDING && s.terminal != ESP32_MQUICKJS_WIFI_RRM_REPORT);
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue neighbor_report(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state,
    const esp32_mquickjs_wifi_rrm_status_t *status)
{
    JSGCRef result_ref, list_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref), *list = JS_PushGCRef(ctx, &list_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    uint8_t token;
    if (!esp32_mquickjs_wifi_rrm_copy(state->request, 0, &token, 1)) goto invalid;
    *result = JS_NewObject(ctx); *list = JS_NewArray(ctx, 0);
    if (JS_IsException(*result) || JS_IsException(*list)) goto fail;
    unsigned count = 0, skipped = 0;
    for (size_t offset = 1; offset < status->received_bytes;) {
        uint8_t header[2], body[255];
        if (status->received_bytes - offset < 2 || !esp32_mquickjs_wifi_rrm_copy(state->request, offset, header, 2)) goto invalid;
        offset += 2;
        if (header[1] > status->received_bytes - offset) goto invalid;
        if (header[0] != 52) { ++skipped; offset += header[1]; continue; }
        if (count == ESP32_MQUICKJS_WIFI_WATCH_MAX_NEIGHBORS ||
            !esp32_mquickjs_wifi_rrm_copy(state->request, offset, body, header[1])) goto invalid;
        offset += header[1];
        esp32_mquickjs_wifi_neighbor_t entry;
        if (!esp32_mquickjs_wifi_neighbor_decode(&entry, body, header[1])) goto invalid;
        char address[18];
        snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x", entry.bssid[0], entry.bssid[1],
            entry.bssid[2], entry.bssid[3], entry.bssid[4], entry.bssid[5]);
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item)) goto fail;
        SET(item, "bssid", JS_NewString(ctx, address));
        SET(item, "bssidInformation", JS_NewUint32(ctx, entry.bssid_information));
        SET(item, "operatingClass", JS_NewUint32(ctx, entry.operating_class));
        SET(item, "channel", JS_NewUint32(ctx, entry.channel));
        SET(item, "phyTypeId", JS_NewUint32(ctx, entry.phy_type));
        SET(item, "candidatePreference", entry.has_preference ? JS_NewUint32(ctx, entry.preference) : JS_NULL);
        SET(item, "skippedSubelements", JS_NewUint32(ctx, entry.skipped_subelements));
        if (JS_IsException(JS_SetPropertyUint32(ctx, *list, count++, *item))) goto fail;
    }
    SET(result, "sequence", JS_NewUint32(ctx, status->operation.identity));
    SET(result, "radioGeneration", JS_NewUint32(ctx, status->operation.generation));
    SET(result, "correlation", JS_NewString(ctx, "sdk-dialog-token"));
    SET(result, "dialogToken", JS_NewUint32(ctx, token));
    SET(result, "reportLength", JS_NewUint32(ctx, status->received_bytes));
    SET(result, "skippedElements", JS_NewUint32(ctx, skipped));
    SET(result, "neighbors", *list);
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &list_ref); return JS_PopGCRef(ctx, &result_ref);
invalid:
    (void)neighbor_error(ctx, state, "WIFI_NEIGHBOR_INVALID_REPORT", "report-decode");
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &list_ref); JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}
static JSValue neighbor_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_wifi_rrm_status_t status;
    (void)esp32_mquickjs_wifi_rrm_status(state->request, &status);
    if (state->error || status.error || status.closed)
        return neighbor_error(ctx, state, status.closed ? "WIFI_NEIGHBOR_CLOSED" : "WIFI_NEIGHBOR_FAILED", NULL);
    if (!state->opening) return neighbor_report(ctx, state, &status);
    JSValue value = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_NEIGHBOR_REQUEST);
    if (JS_IsException(value)) return value;
    if (!esp32_mquickjs_wifi_rrm_retain(state->request)) return JS_ThrowInternalError(ctx, "request references exhausted");
    JS_SetOpaque(ctx, value, state->request);
    state->transferred = true;
    return value;
}
static esp32_mquickjs_cancel_result_t neighbor_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    esp32_mquickjs_wifi_rrm_cancel(state->request, false);
    return ESP32_MQUICKJS_CANCEL_REQUESTED;
}
static uint32_t neighbor_timeout(const esp32_mquickjs_future_driver_state_t *state) { return state->timeout_ms; }
static JSValue neighbor_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    state->cancelled = true;
    esp32_mquickjs_wifi_rrm_cancel(state->request, true);
    return neighbor_error(ctx, state, "WIFI_NEIGHBOR_TIMEOUT", "deadline");
}
#define DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = neighbor_start, .poll = neighbor_poll, .finish = neighbor_finish, \
    .cancel = neighbor_cancel, .destroy = neighbor_destroy, .timeout_ms = neighbor_timeout, .on_timeout = neighbor_on_timeout }
static const esp32_mquickjs_future_driver_t s_neighbor_open_driver = DRIVER(neighbor_open_capture);
static const esp32_mquickjs_future_driver_t s_neighbor_receive_driver = DRIVER(neighbor_receive_capture);
static JSValue neighbor_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver; *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref); return result;
}
JSValue js_wifi_neighbor_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return neighbor_call(ctx, self, argc, argv, "receive"); }
JSValue js_wifi_neighbor_request(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    JSGCRef global_ref, wifi_ref, module_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *wifi = JS_PushGCRef(ctx, &wifi_ref);
    JSValue *module = JS_PushGCRef(ctx, &module_ref);
    *global = JS_GetGlobalObject(ctx);
    *wifi = JS_IsException(*global) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *global, "wifi");
    *module = JS_IsException(*wifi) ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *wifi, "roaming");
    JSValue result = JS_IsException(*module) ? JS_EXCEPTION : neighbor_call(ctx, module, argc, argv, "requestNeighborReport");
    JS_PopGCRef(ctx, &module_ref); JS_PopGCRef(ctx, &wifi_ref); JS_PopGCRef(ctx, &global_ref); return result;
}
bool esp32_mquickjs_init_wifi_neighbor_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, method_ref, child_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref), *child = JS_PushGCRef(ctx, &child_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "wifi");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "roaming");
    if (JS_IsException(*child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "requestNeighborReport");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_neighbor_open_driver)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiNeighborReportRequest");
    if (JS_IsException(*object)) goto done;
    *child = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*child)) goto done;
    *method = JS_GetPropertyStr(ctx, *child, "receive");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_neighbor_receive_driver)) goto done;
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register Neighbor Report runtime");
    JS_PopGCRef(ctx, &child_ref); JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
