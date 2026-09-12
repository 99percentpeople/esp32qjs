#include "esp32_mquickjs_wifi_wps.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_wifi_wps_session.h"
#include "esp32_mquickjs_wifi_wps_ap.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_event_queue.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_wireless_core.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

typedef enum { WPS_RECEIVE, WPS_CLOSE } wps_operation_t;
typedef struct {
    esp_wps_config_t config;
    uint32_t timeout_ms;
    bool allow_ap_channel_change;
} wps_options_t;
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_wps_session_t *session;
    wps_operation_t operation;
    uint32_t timeout_ms;
    int64_t deadline_us;
    bool started, cancelled, wait_timed_out, observation_registered;
};
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

/* Copies exactly one validated UTF-8 string. No JS allocation occurs while the
 * temporary C-string pointer is live; diagnostics never contain its contents. */
static bool wps_string(JSContext *ctx, JSValue value, char *out, size_t capacity)
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer;
    size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (!text || length >= capacity || memchr(text, 0, length)) return false;
    memcpy(out, text, length); out[length] = 0;
    return true;
}

static bool wps_options_for_role(JSContext *ctx, JSGCRef *root, wps_options_t *options, bool registrar)
{
    static const char *const keys[] = {"method", "pin", "device", "timeoutMs", "allowApChannelChange"};
    static const char *const methods[] = {"pbc", "pin"};
    *options = (wps_options_t){.config = {.wps_type = WPS_TYPE_PBC}, .timeout_ms = 120000};
    JSGCRef ref, device_ref;
    JSValue *field = JS_PushGCRef(ctx, &ref), *device = JS_PushGCRef(ctx, &device_ref);
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, root->val, registrar ? "wifi.wps.startAP" : "wifi.wps.start", keys,
        sizeof(keys) / sizeof(keys[0]) - (registrar ? 1U : 0U))) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, root->val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("method");
    if (!JS_IsUndefined(*field)) {
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *field, methods, 2, &index)) goto invalid;
        options->config.wps_type = index ? WPS_TYPE_PIN : WPS_TYPE_PBC;
    }
    FIELD("pin");
    if (!JS_IsUndefined(*field)) {
        if (options->config.wps_type != WPS_TYPE_PIN ||
            !wps_string(ctx, *field, options->config.pin, sizeof(options->config.pin)) ||
            strlen(options->config.pin) != 8) goto invalid;
    }
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 3600000, &options->timeout_ms)) goto invalid;
    *field = JS_UNDEFINED;
    if (!registrar) { FIELD("allowApChannelChange"); }
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        options->allow_ap_channel_change = *field == JS_TRUE;
    }
    FIELD("device"); *device = *field;
    if (!JS_IsUndefined(*device)) {
        static const char *const names[] = {"manufacturer", "modelNumber", "modelName", "deviceName"};
        wps_factory_information_t *f = &options->config.factory_info;
        char *outputs[] = {f->manufacturer, f->model_number, f->model_name, f->device_name};
        const size_t sizes[] = {sizeof(f->manufacturer), sizeof(f->model_number), sizeof(f->model_name), sizeof(f->device_name)};
        if (!esp32_mquickjs_validate_plain_options(ctx, *device, "wifi.wps.start device", names, 4)) goto done;
        for (unsigned i = 0; i < 4; ++i) {
            *field = JS_GetPropertyStr(ctx, *device, names[i]);
            if (JS_IsException(*field)) goto done;
            if (!JS_IsUndefined(*field) && !wps_string(ctx, *field, outputs[i], sizes[i])) goto invalid;
        }
    }
    if (!esp32_mquickjs_wifi_wps_config_valid(&options->config)) goto invalid;
    ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid WPS options");
done:
    if (!ok) esp32_mquickjs_wireless_secure_zero(options, sizeof(*options));
    JS_PopGCRef(ctx, &device_ref); JS_PopGCRef(ctx, &ref); return ok;
#undef FIELD
}

static bool wps_options(JSContext *ctx, JSGCRef *root, wps_options_t *options)
{
    return wps_options_for_role(ctx, root, options, false);
}
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
bool esp32_mquickjs_wifi_wps_ap_capture_options(JSContext *ctx, JSGCRef *root,
    esp_wps_config_t *config, uint32_t *timeout_ms)
{
    wps_options_t options;
    bool ok = wps_options_for_role(ctx, root, &options, true);
    if (ok) { *config = options.config; *timeout_ms = options.timeout_ms; }
    esp32_mquickjs_wireless_secure_zero(&options, sizeof(options));
    return ok;
}
#endif

static esp32_mquickjs_wifi_wps_session_t *wps_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_WPS_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiWpsSession"); return NULL;
    }
    esp32_mquickjs_wifi_wps_session_t *session = JS_GetOpaque(ctx, value);
    if (!session) JS_ThrowReferenceError(ctx, "invalid WiFiWpsSession");
    return session;
}
void js_wifi_wps_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    /* Pending Future references keep the session alive. The registry requests
     * close when the final external reference, including those Futures, drops. */
    esp32_mquickjs_wifi_wps_session_release(opaque);
}
JSValue js_wifi_wps_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.wps.start()");
}
static JSValue wps_identity(JSContext *ctx, uint64_t identity)
{
    if (!identity) return JS_NULL;
    JSGCRef ref;
    JSValue *value = JS_PushGCRef(ctx, &ref);
    *value = JS_NewObject(ctx);
    if (JS_IsException(*value)) goto fail;
    SET(value, "low", JS_NewUint32(ctx, (uint32_t)identity));
    SET(value, "high", JS_NewUint32(ctx, (uint32_t)(identity >> 32)));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue wps_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_wps_session_status_t *s)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "state", JS_NewString(ctx, s->closing ? s->retired ? "closed" : "closing" :
        s->error ? "faulted" : s->credentials_consumed ? "consumed" : s->credentials_ready ? "credentials-ready" :
        s->pin_ready ? "pin-ready" : s->native.worker.native.started ? "negotiating" : "opening"));
    SET(result, "operation", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.identity) : JS_NULL);
    SET(result, "radioGeneration", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.generation) : JS_NULL);
    SET(result, "nativeIdentity", wps_identity(ctx, s->native.worker.native.identity));
    SET(result, "workerBusy", JS_NewBool(s->worker_busy));
    SET(result, "driverStarted", JS_NewBool(s->native.worker.native.started));
    SET(result, "terminalSeen", JS_NewBool(s->native.worker.native.terminal_seen));
    SET(result, "pinReady", JS_NewBool(s->pin_ready && !s->closing && !s->pin_consumed));
    SET(result, "pinConsumed", JS_NewBool(s->pin_consumed));
    SET(result, "credentialsReady", JS_NewBool(s->credentials_ready && !s->closing && !s->credentials_consumed));
    SET(result, "credentialsConsumed", JS_NewBool(s->credentials_consumed));
    SET(result, "captureFinished", JS_NewBool(s->capture_finished));
    SET(result, "nativeClosed", JS_NewBool(s->native_closed));
    SET(result, "helperDrained", JS_NewBool(s->helper_drained));
    SET(result, "configRestored", JS_NewBool(s->native.config_restored));
    SET(result, "channelRestored", JS_NewBool(s->native.channel_restored));
    SET(result, "storageRestored", JS_NewBool(s->native.storage_restored));
    SET(result, "eventFenced", JS_NewBool(s->native.event_fenced));
    SET(result, "closeRequested", JS_NewBool(s->closing));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "handoffUnknown", JS_NewBool(s->native.worker.handoff_unknown));
    SET(result, "trackingFault", JS_NewBool(s->native.worker.native.tracking_fault));
    SET(result, "eventId", JS_NewInt32(ctx, s->native.worker.native.event_id));
    SET(result, "failureReason", JS_NewInt32(ctx, s->native.worker.native.failure_reason));
    SET(result, "nativeErrorStage", JS_NewInt32(ctx, s->native.worker.native.error_stage));
    SET(result, "nativeCleanupStage", JS_NewInt32(ctx, s->native.worker.native.cleanup_stage));
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "stage", s->stage ? JS_NewString(ctx, s->stage) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", s->cleanup_stage ? JS_NewString(ctx, s->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
#define WPS_WATCH_HANDLES 4U
#define WPS_WATCH_CAPACITY 16U
typedef struct {
    uint32_t sequence;
    esp32_mquickjs_wifi_wps_session_status_t status;
} wps_watch_event_t;
typedef struct {
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_wifi_wps_session_t *session;
    wps_watch_event_t last;
    bool context_bound;
} wps_watch_source_t;
static portMUX_TYPE s_wps_watch_lock = portMUX_INITIALIZER_UNLOCKED;
static wps_watch_source_t *s_wps_watch_sources[WPS_WATCH_HANDLES];
static unsigned s_wps_watch_handles;

/* Compare semantic fields, never padding or the transient worker-busy bit.
 * Every queued pointer is a framework stage string with static lifetime. */
static bool wps_watch_same(const esp32_mquickjs_wifi_wps_session_status_t *a,
    const esp32_mquickjs_wifi_wps_session_status_t *b)
{
#define SAME(field) if (a->field != b->field) return false
    SAME(error);
    SAME(cleanup_error);
    SAME(stage);
    SAME(cleanup_stage);
    SAME(pin_ready);
    SAME(pin_consumed);
    SAME(credentials_ready);
    SAME(credentials_consumed);
    SAME(closing);
    SAME(retired);
    SAME(timed_out);
    SAME(capture_finished);
    SAME(native_closed);
    SAME(helper_drained);
    SAME(native.operation.identity);
    SAME(native.operation.generation);
    SAME(native.worker.native.identity);
    SAME(native.worker.native.started);
    SAME(native.worker.native.terminal_seen);
    SAME(native.worker.native.tracking_fault);
    SAME(native.worker.native.event_id);
    SAME(native.worker.native.failure_reason);
    SAME(native.worker.native.error_stage);
    SAME(native.worker.native.cleanup_stage);
    SAME(native.worker.handoff_unknown);
    SAME(native.config_restored);
    SAME(native.channel_restored);
    SAME(native.storage_restored);
    SAME(native.event_fenced);
#undef SAME
    return true;
}

static void wps_watch_closed(void *opaque)
{
    wps_watch_source_t *source = opaque;
    portENTER_CRITICAL(&s_wps_watch_lock);
    for (unsigned i = 0; i < WPS_WATCH_HANDLES; ++i)
        if (s_wps_watch_sources[i] == source) s_wps_watch_sources[i] = NULL;
    esp32_mquickjs_wifi_wps_session_t *session = source->session;
    source->session = NULL;
    bool bound = source->context_bound;
    if (!bound) --s_wps_watch_handles;
    portEXIT_CRITICAL(&s_wps_watch_lock);
    if (session) esp32_mquickjs_wifi_wps_session_release(session);
    /* Only a private queue construction failure uses this path. No receiver
     * or producer has seen it; its deferred close owns the failed context. */
    if (!bound) esp32_mquickjs_memory_payload_free(source);
}

static void wps_watch_destroyed(void *opaque)
{
    portENTER_CRITICAL(&s_wps_watch_lock);
    --s_wps_watch_handles;
    portEXIT_CRITICAL(&s_wps_watch_lock);
    esp32_mquickjs_memory_payload_free(opaque); /* Budget survives close until queue storage dies. */
}

static JSValue wps_watch_to_js(JSContext *ctx, const void *data, void *opaque)
{
    (void)opaque;
    const wps_watch_event_t *event = data;
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, event->sequence));
    SET(result, "status", wps_status_to_js(ctx, &event->status));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

bool esp32_mquickjs_wifi_wps_poll_observations(bool close)
{
    bool handled = false;
    for (unsigned i = 0; i < WPS_WATCH_HANDLES; ++i) {
        esp32_mquickjs_event_queue_t *queue = NULL;
        esp32_mquickjs_wifi_wps_session_t *session = NULL, *detached = NULL;
        portENTER_CRITICAL(&s_wps_watch_lock);
        wps_watch_source_t *source = s_wps_watch_sources[i];
        if (source && esp32_mquickjs_event_queue_retain(source->queue)) {
            queue = source->queue;
            if (!close && esp32_mquickjs_wifi_wps_session_retain(source->session)) session = source->session;
        } else if (source && close) {
            /* A disposed queue already belongs to its reaper. Detach now;
             * its later close callback sees NULL and cannot double-release. */
            s_wps_watch_sources[i] = NULL;
            detached = source->session; source->session = NULL;
        }
        portEXIT_CRITICAL(&s_wps_watch_lock);
        if (detached) esp32_mquickjs_wifi_wps_session_release(detached);
        if (!queue) continue;
        if (close) {
            handled = esp32_mquickjs_event_queue_close(queue) || handled;
        } else if (session) {
            wps_watch_event_t event = {0};
            if (esp32_mquickjs_wifi_wps_session_observation(session, &event.status) &&
                (!source->last.sequence || !wps_watch_same(&source->last.status, &event.status))) {
                if (source->last.sequence == UINT32_MAX) esp32_mquickjs_event_queue_request_close(queue);
                else {
                    event.sequence = source->last.sequence + 1U;
                    source->last = event; /* Drop-newest never retries an old snapshot. */
                    (void)esp32_mquickjs_event_queue_try_send_from_callback(queue, &event);
                    handled = true;
                }
            }
            esp32_mquickjs_wifi_wps_session_release(session);
        }
        esp32_mquickjs_event_queue_release(queue);
    }
    return handled;
}

JSValue js_wifi_wps_watch(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    esp32_mquickjs_wifi_wps_session_t *session = wps_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_wps_session_retain(session))
        return JS_ThrowInternalError(ctx, "Wps reference exhausted");
    JSGCRef options_ref, field_ref, queue_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *field = JS_PushGCRef(ctx, &field_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    uint32_t capacity = 8;
    if (argc > 1) goto invalid;
    *options = argc ? argv[0] : JS_UNDEFINED;
    if (!JS_IsUndefined(*options)) {
        static const char *const keys[] = {"capacity"};
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, "WiFiWpsSession.watch", keys, 1)) goto fail;
        *field = JS_GetPropertyStr(ctx, *options, "capacity");
        if (JS_IsException(*field)) goto fail;
        if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, WPS_WATCH_CAPACITY, &capacity)) goto invalid;
    }
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (!runtime) { JS_ThrowInternalError(ctx, "Wps watch requires an active runtime"); goto fail; }
    wps_watch_source_t *source = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*source), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!source) { JS_ThrowOutOfMemory(ctx); goto fail; }
    unsigned slot = WPS_WATCH_HANDLES;
    bool duplicate = false;
    portENTER_CRITICAL(&s_wps_watch_lock);
    for (unsigned i = 0; i < WPS_WATCH_HANDLES; ++i) {
        if (!s_wps_watch_sources[i]) slot = i;
        else if (s_wps_watch_sources[i]->session == session) duplicate = true;
    }
    bool available = !duplicate && slot != WPS_WATCH_HANDLES && s_wps_watch_handles < WPS_WATCH_HANDLES;
    if (available) ++s_wps_watch_handles;
    portEXIT_CRITICAL(&s_wps_watch_lock);
    if (!available) {
        esp32_mquickjs_memory_payload_free(source); JS_ThrowInternalError(ctx, "Wps watch already open or queue budget retained"); goto fail;
    }
    source->session = session; session = NULL; /* Context owns the retained reference. */
    *queue = esp32_mquickjs_event_queue_new_wireless("wifi", ctx, runtime, sizeof(wps_watch_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, wps_watch_to_js, NULL, wps_watch_closed, source);
    if (JS_IsException(*queue)) { wps_watch_closed(source); goto fail; }
    source->queue = esp32_mquickjs_event_queue_from_value(ctx, *queue);
    source->context_bound = esp32_mquickjs_event_queue_bind_context_release(source->queue, wps_watch_destroyed);
    if (!source->context_bound) {
        /* Dispose owns deferred close; do not free its opaque context here. */
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
        JS_ThrowInternalError(ctx, "Wps queue lifetime binding failed"); goto fail;
    }
    portENTER_CRITICAL(&s_wps_watch_lock);
    s_wps_watch_sources[slot] = source;
    portEXIT_CRITICAL(&s_wps_watch_lock);
    JSValue result = JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref);
    return result;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Wps watch capacity must be an integer in 1..16");
fail:
    if (session) esp32_mquickjs_wifi_wps_session_release(session);
    JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}

static JSValue wps_error(JSContext *ctx, const char *operation, esp32_mquickjs_wifi_wps_session_t *session,
    esp_err_t error, bool wait_timeout)
{
    esp32_mquickjs_wifi_wps_session_status_t status = {0};
    if (session) (void)esp32_mquickjs_wifi_wps_session_status(session, &status);
    if (error == ESP_OK) error = status.error ? status.error : ESP_ERR_INVALID_STATE;
    bool timeout = wait_timeout || status.timed_out || error == ESP_ERR_TIMEOUT;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = wps_status_to_js(ctx, &status);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(wait_timeout));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout ? "WIFI_WPS_TIMEOUT" :
        status.closing && status.error == ESP_OK ? "WIFI_WPS_CLOSED" : "WIFI_WPS_FAILED",
        operation, "Wps operation did not complete; inspect session status", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_wps_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "Wps status expects no arguments");
    esp32_mquickjs_wifi_wps_session_t *session = wps_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_wps_session_status_t status;
    (void)esp32_mquickjs_wifi_wps_session_status(session, &status);
    return wps_status_to_js(ctx, &status);
}
JSValue js_wifi_wps_global_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "Wps status expects no arguments");
    esp32_mquickjs_wifi_wps_global_status_t status;
    esp32_mquickjs_wifi_wps_global_status(&status);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "active", JS_NewBool(status.active));
    SET(result, "handles", JS_NewUint32(ctx, status.handles));
    SET(result, "workers", JS_NewUint32(ctx, status.workers));
    SET(result, "runtimeClosing", JS_NewBool(status.runtime_closing));
    SET(result, "session", status.active ? wps_status_to_js(ctx, &status.session) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_wps_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "Wps capabilities expects no arguments");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-wps/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "enrollee", JS_TRUE);
    SET(result, "pbc", JS_TRUE);
    SET(result, "pin", JS_TRUE);
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    SET(result, "apRegistrar", JS_TRUE);
    SET(result, "maxAPSessions", JS_NewInt32(ctx, 4));
    SET(result, "maxAPWatchQueues", JS_NewInt32(ctx, 4));
    SET(result, "sdkApRegistrar", JS_TRUE);
#else
    SET(result, "apRegistrar", JS_FALSE);
    SET(result, "maxAPSessions", JS_NewInt32(ctx, 0));
    SET(result, "maxAPWatchQueues", JS_NewInt32(ctx, 0));
    SET(result, "sdkApRegistrar", JS_FALSE);
#endif
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR && CONFIG_SOC_WIFI_SUPPORT_5G
    SET(result, "apRegistrar5GHz", JS_TRUE);
#else
    SET(result, "apRegistrar5GHz", JS_FALSE);
#endif
    SET(result, "autoConnect", JS_FALSE);
    SET(result, "sdkTimeoutMs", JS_NewInt32(ctx, 120000));
    SET(result, "sdkTimeoutConfigurable", JS_FALSE);
    SET(result, "maxCredentials", JS_NewInt32(ctx, ESP32_MQUICKJS_WPS_CREDENTIALS_MAX));
    SET(result, "observations", JS_TRUE);
    SET(result, "maxWatchQueues", JS_NewInt32(ctx, WPS_WATCH_HANDLES));
    SET(result, "maxWatchCapacity", JS_NewInt32(ctx, WPS_WATCH_CAPACITY));
    SET(result, "maxSessions", JS_NewUint32(ctx, ESP32_MQUICKJS_WPS_MAX_HANDLES));
    SET(result, "maxActiveSessions", JS_NewInt32(ctx, 1));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_wps_start(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.wps.start expects options");
    JSGCRef options_ref, result_ref;
    JSValue *input = JS_PushGCRef(ctx, &options_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *input = argv[0];
    wps_options_t options;
    esp32_mquickjs_wifi_wps_session_t *session = NULL;
    if (!wps_options(ctx, &options_ref, &options)) goto fail;
    esp_err_t error = esp32_mquickjs_wifi_wps_session_create(&options.config,
        options.allow_ap_channel_change, options.timeout_ms, &session);
    esp32_mquickjs_wireless_secure_zero(&options, sizeof(options));
    if (error != ESP_OK) { (void)wps_error(ctx, "wifi.wps.start", session, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_WPS_SESSION);
    if (JS_IsException(*result)) goto fail;
    JS_SetOpaque(ctx, *result, session);
    error = esp32_mquickjs_wifi_wps_session_activate(session);
    if (error != ESP_OK) {
        JS_SetOpaque(ctx, *result, NULL);
        (void)wps_error(ctx, "wifi.wps.start", session, error, false); goto fail;
    }
    /* No JS allocations after activation. Native worker owns all RF calls. */
    (void)esp32_mquickjs_wifi_wps_service();
    JSValue value = JS_PopGCRef(ctx, &result_ref);
    JS_PopGCRef(ctx, &options_ref); return value;
fail:
    esp32_mquickjs_wifi_wps_session_release(session);
    JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &options_ref); return JS_EXCEPTION;
}

static JSValue wps_bytes(JSContext *ctx, const uint8_t *bytes, size_t length)
{
    JSGCRef ref;
    JSValue *array = JS_PushGCRef(ctx, &ref);
    *array = JS_NewArray(ctx, 0);
    if (JS_IsException(*array)) goto fail;
    for (size_t i = 0; i < length; ++i)
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array, i, JS_NewInt32(ctx, bytes[i])))) goto fail;
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
static JSValue wps_pin_to_js(JSContext *ctx, esp32_mquickjs_wifi_wps_session_t *session)
{
    uint8_t pin[8] = {0};
    esp_err_t error = esp32_mquickjs_wifi_wps_session_pin(session, pin, false);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    if (error != ESP_OK) { (void)wps_error(ctx, "WiFiWpsSession.receive", session, error, false); goto fail; }
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "type", JS_NewString(ctx, "pin"));
    SET(result, "pin", JS_NewStringLen(ctx, (const char *)pin, sizeof(pin)));
    error = esp32_mquickjs_wifi_wps_session_pin(session, NULL, true);
    if (error != ESP_OK) { (void)wps_error(ctx, "WiFiWpsSession.receive", session, error, false); goto fail; }
    esp32_mquickjs_wireless_secure_zero(pin, sizeof(pin));
    return JS_PopGCRef(ctx, &ref);
fail:
    esp32_mquickjs_wireless_secure_zero(pin, sizeof(pin));
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue wps_credentials_to_js(JSContext *ctx, esp32_mquickjs_wifi_wps_session_t *session)
{
    esp32_mquickjs_wifi_wps_credentials_t credentials = {0};
    esp_err_t error = esp32_mquickjs_wifi_wps_session_credentials(session, &credentials, false);
    JSGCRef ref, entries_ref, entry_ref;
    JSValue *result = JS_PushGCRef(ctx, &ref), *entries = JS_PushGCRef(ctx, &entries_ref);
    JSValue *entry = JS_PushGCRef(ctx, &entry_ref);
    if (error != ESP_OK) { (void)wps_error(ctx, "WiFiWpsSession.receive", session, error, false); goto fail; }
    if (!credentials.count || credentials.count > ESP32_MQUICKJS_WPS_CREDENTIALS_MAX) goto invalid;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "type", JS_NewString(ctx, "credentials"));
    *entries = JS_NewArray(ctx, 0);
    if (JS_IsException(*entries)) goto fail;
    for (unsigned i = 0; i < credentials.count; ++i) {
        const esp32_mquickjs_wifi_wps_credential_t *c = &credentials.entries[i];
        if (c->ssid_length > sizeof(c->ssid) || c->password_length > sizeof(c->password)) goto invalid;
        *entry = JS_NewObject(ctx);
        if (JS_IsException(*entry)) goto fail;
        SET(entry, "ssidBytes", wps_bytes(ctx, c->ssid, c->ssid_length));
        SET(entry, "passwordBytes", wps_bytes(ctx, c->password, c->password_length));
        SET(entry, "authType", JS_NewUint32(ctx, c->auth_type));
        SET(entry, "encryptionType", JS_NewUint32(ctx, c->encryption_type));
        SET(entry, "keyIndex", JS_NewUint32(ctx, c->key_index));
        char address[18];
        snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x", c->mac[0],
            c->mac[1], c->mac[2], c->mac[3], c->mac[4], c->mac[5]);
        SET(entry, "mac", JS_NewString(ctx, address));
        if (JS_IsException(JS_SetPropertyUint32(ctx, *entries, i, *entry))) goto fail;
    }
    SET(result, "credentials", *entries);
    error = esp32_mquickjs_wifi_wps_session_credentials(session, NULL, true);
    if (error != ESP_OK) { (void)wps_error(ctx, "WiFiWpsSession.receive", session, error, false); goto fail; }
    esp32_mquickjs_wireless_secure_zero(&credentials, sizeof(credentials));
    JS_PopGCRef(ctx, &entry_ref); JS_PopGCRef(ctx, &entries_ref); return JS_PopGCRef(ctx, &ref);
invalid:
    JS_ThrowInternalError(ctx, "invalid native WPS credential lengths");
fail:
    esp32_mquickjs_wireless_secure_zero(&credentials, sizeof(credentials));
    JS_PopGCRef(ctx, &entry_ref); JS_PopGCRef(ctx, &entries_ref); JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static const char *wps_operation_name(wps_operation_t operation)
{ return operation == WPS_RECEIVE ? "WiFiWpsSession.receive" : "WiFiWpsSession.close"; }
static void wps_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state) return;
    if (state->observation_registered)
        esp32_mquickjs_wifi_wps_session_wait_end(state->session, state->operation == WPS_CLOSE);
    esp32_mquickjs_wifi_wps_session_release(state->session);
    esp32_mquickjs_memory_payload_free(state);
}
static bool wps_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out, wps_operation_t operation)
{
    *out = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "Wps method expects optional timeout options"); return false; }
    esp32_mquickjs_wifi_wps_session_t *session = wps_receiver(ctx, self->val);
    if (!session) return false;
    if (!esp32_mquickjs_wifi_wps_session_retain(session)) { JS_ThrowInternalError(ctx, "Wps reference exhausted"); return false; }
    bool valid = true;
    uint32_t timeout = 1000;
    if (argc && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        valid = esp32_mquickjs_validate_plain_options(ctx, argv[0].val, wps_operation_name(operation), keys, 1);
        if (valid) {
            JSGCRef ref;
            JSValue *field = JS_PushGCRef(ctx, &ref);
            *field = JS_GetPropertyStr(ctx, argv[0].val, "timeoutMs");
            valid = !JS_IsException(*field) && (JS_IsUndefined(*field) ||
                esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 3600000, &timeout));
            JS_PopGCRef(ctx, &ref);
        }
    }
    if (!valid) {
        esp32_mquickjs_wifi_wps_session_release(session);
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid Wps timeout options");
        return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { esp32_mquickjs_wifi_wps_session_release(session); JS_ThrowOutOfMemory(ctx); return false; }
    state->session = session; state->operation = operation; state->timeout_ms = timeout;
    if (!esp32_mquickjs_wifi_wps_session_wait_begin(session, operation == WPS_CLOSE)) {
        wps_destroy(state); JS_ThrowInternalError(ctx, "Wps waiter count exhausted"); return false;
    }
    state->observation_registered = true;
    *out = state; return true;
}
static bool wps_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return wps_capture(ctx, self, argc, argv, out, WPS_RECEIVE); }
static bool wps_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return wps_capture(ctx, self, argc, argv, out, WPS_CLOSE); }
static bool wps_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)runtime; (void)token;
    int64_t now = esp_timer_get_time(), duration = (int64_t)state->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) { JS_ThrowInternalError(ctx, "Wps clock exhausted"); return false; }
    state->started = true; state->deadline_us = now + duration;
    if (state->operation == WPS_CLOSE) esp32_mquickjs_wifi_wps_session_close(state->session, false);
    (void)esp32_mquickjs_wifi_wps_service();
    return true;
}
static esp32_mquickjs_future_poll_t wps_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled) return ESP32_MQUICKJS_FUTURE_READY;
    (void)esp32_mquickjs_wifi_wps_service();
    esp32_mquickjs_wifi_wps_session_status_t status;
    (void)esp32_mquickjs_wifi_wps_session_status(state->session, &status);
    bool ready = state->operation == WPS_CLOSE ? status.retired : status.error || status.closing ||
        status.credentials_consumed || ((status.pin_ready || status.credentials_ready) && !status.worker_busy);
    if (!ready && state->operation == WPS_RECEIVE && esp_timer_get_time() >= state->deadline_us) {
        state->wait_timed_out = true; ready = true;
    }
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue wps_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->operation == WPS_CLOSE) return JS_UNDEFINED;
    if (state->wait_timed_out) return JS_NULL;
    esp32_mquickjs_wifi_wps_session_status_t status;
    (void)esp32_mquickjs_wifi_wps_session_status(state->session, &status);
    if (status.error || status.closing) return wps_error(ctx, wps_operation_name(state->operation), state->session, ESP_OK, false);
    if (status.pin_ready && !status.pin_consumed) return wps_pin_to_js(ctx, state->session);
    if (status.credentials_consumed) return JS_ThrowReferenceError(ctx, "WPS credentials already consumed");
    return wps_credentials_to_js(ctx, state->session);
}
static esp32_mquickjs_cancel_result_t wps_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    /* receive cancellation changes no native intent. Started close is already
     * retained by the registry; cancellation before start must not close. */
    return ESP32_MQUICKJS_CANCELLED;
}
static uint32_t wps_timeout(const esp32_mquickjs_future_driver_state_t *state)
{ return state->operation == WPS_RECEIVE ? 0U : state->timeout_ms; }
static JSValue wps_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    (void)wps_cancel(state);
    return wps_error(ctx, wps_operation_name(state->operation), state->session, ESP_ERR_TIMEOUT, true);
}
#define WPS_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = wps_start, .poll = wps_poll, .finish = wps_finish, \
    .cancel = wps_cancel, .destroy = wps_destroy, .timeout_ms = wps_timeout, .on_timeout = wps_on_timeout }
static const esp32_mquickjs_future_driver_t s_wps_receive_driver = WPS_DRIVER(wps_receive_capture);
static const esp32_mquickjs_future_driver_t s_wps_close_driver = WPS_DRIVER(wps_close_capture);
static JSValue wps_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver; *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref); return result;
}
JSValue js_wifi_wps_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "WPS cancel expects no arguments");
    esp32_mquickjs_wifi_wps_session_t *session = wps_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_wps_session_close(session, false);
    (void)esp32_mquickjs_wifi_wps_service();
    return JS_UNDEFINED;
}
JSValue js_wifi_wps_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return wps_call(ctx, self, argc, argv, "receive"); }
JSValue js_wifi_wps_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return wps_call(ctx, self, argc, argv, "close"); }
bool esp32_mquickjs_init_wifi_wps_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, proto_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *proto = JS_PushGCRef(ctx, &proto_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiWpsSession");
    if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "receive");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_wps_receive_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_wps_close_driver)) goto done;
    ok = true;
#if CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
    ok = esp32_mquickjs_init_wifi_wps_ap_runtime(ctx, runtime);
#endif
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register Wps runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &proto_ref); JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
