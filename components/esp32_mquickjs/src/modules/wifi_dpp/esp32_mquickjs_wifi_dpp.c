#include "esp32_mquickjs_wifi_dpp.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_wifi_dpp_session.h"
#include "esp32_mquickjs_wifi.h"
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

typedef enum { DPP_RECEIVE, DPP_CLOSE, DPP_CONNECT } dpp_operation_t;
typedef struct {
    esp32_mquickjs_wifi_dpp_worker_options_t config;
    uint32_t timeout_ms;
    bool allow_ap_channel_change;
} dpp_options_t;
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_dpp_session_t *session;
    dpp_operation_t operation;
    uint32_t timeout_ms;
    int64_t deadline_us;
    unsigned configuration_index;
    esp32_mquickjs_wifi_dpp_auth_t authentication;
    bool started, cancelled, wait_timed_out, observation_registered, allow_ap_restart;
};
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

/* Copies exactly one validated UTF-8 string. No JS allocation occurs while the
 * temporary C-string pointer is live; diagnostics never contain its contents. */
static bool dpp_string(JSContext *ctx, JSValue value, char *out, size_t capacity)
{
    if (!JS_IsString(ctx, value)) return false;
    JSCStringBuf buffer;
    size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, value, &buffer);
    if (!text || length >= capacity || memchr(text, 0, length)) return false;
    memcpy(out, text, length); out[length] = 0;
    return true;
}

static bool dpp_options(JSContext *ctx, JSGCRef *root, dpp_options_t *options)
{
    static const char *const keys[] = {"channels", "privateKeyHexDer", "info", "timeoutMs", "allowApChannelChange"};
    *options = (dpp_options_t){.config = {.channels = "6"}, .timeout_ms = 120000};
    JSGCRef field_ref, list_ref;
    JSValue *field = JS_PushGCRef(ctx, &field_ref), *list = JS_PushGCRef(ctx, &list_ref);
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, root->val, "wifi.dpp.startEnrollee", keys, 5)) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, root->val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("channels"); *list = *field;
    if (!JS_IsUndefined(*list)) {
        if (!JS_IsArray(ctx, *list)) goto invalid;
        *field = JS_GetPropertyStr(ctx, *list, "length");
        uint32_t count;
        if (JS_IsException(*field)) goto done;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, ESP_DPP_MAX_CHAN_COUNT, &count)) goto invalid;
        size_t used = 0;
        uint8_t channels[ESP_DPP_MAX_CHAN_COUNT] = {0};
        for (uint32_t i = 0; i < count; ++i) {
            *field = JS_GetPropertyUint32(ctx, *list, i);
            uint32_t channel;
            if (JS_IsException(*field)) goto done;
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 255, &channel)) goto invalid;
            for (uint32_t j = 0; j < i; ++j) if (channels[j] == channel) goto invalid;
            channels[i] = (uint8_t)channel;
            int n = snprintf(options->config.channels + used, sizeof(options->config.channels) - used,
                "%s%u", i ? "," : "", (unsigned)channel);
            if (n < 0 || (size_t)n >= sizeof(options->config.channels) - used) goto invalid;
            used += (size_t)n;
        }
    }
    FIELD("privateKeyHexDer");
    if (!JS_IsUndefined(*field)) {
        if (!dpp_string(ctx, *field, options->config.private_key_hex_der, sizeof(options->config.private_key_hex_der))) goto invalid;
        options->config.has_key = true;
    }
    FIELD("info");
    if (!JS_IsUndefined(*field)) {
        if (!dpp_string(ctx, *field, options->config.info, sizeof(options->config.info))) goto invalid;
        options->config.has_info = true;
    }
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 3600000, &options->timeout_ms)) goto invalid;
    FIELD("allowApChannelChange");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        options->allow_ap_channel_change = *field == JS_TRUE;
    }
    if (esp32_mquickjs_wifi_dpp_worker_validate(&options->config) != ESP_OK) goto invalid;
    ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid DPP options");
done:
    if (!ok) esp32_mquickjs_wireless_secure_zero(options, sizeof(*options));
    JS_PopGCRef(ctx, &list_ref); JS_PopGCRef(ctx, &field_ref); return ok;
#undef FIELD
}

static esp32_mquickjs_wifi_dpp_session_t *dpp_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_DPP_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiDppSession"); return NULL;
    }
    esp32_mquickjs_wifi_dpp_session_t *session = JS_GetOpaque(ctx, value);
    if (!session) JS_ThrowReferenceError(ctx, "invalid WiFiDppSession");
    return session;
}
void js_wifi_dpp_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    /* Pending Future references keep the session alive. The registry requests
     * close when the final external reference, including those Futures, drops. */
    esp32_mquickjs_wifi_dpp_session_release(opaque);
}
JSValue js_wifi_dpp_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.dpp.startEnrollee()");
}
static JSValue dpp_identity(JSContext *ctx, uint64_t identity)
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
static JSValue dpp_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_dpp_session_status_t *s)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "state", JS_NewString(ctx, s->closing ? s->retired ? "closed" : "closing" :
        s->error ? "faulted" : s->connected ? "connected" : s->connection_requested ? "connecting" :
        s->configs_consumed ? "consumed" : s->configs_ready ? "configurations-ready" :
        s->uri_ready ? "uri-ready" : s->listening ? "negotiating" : "opening"));
    SET(result, "operation", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.identity) : JS_NULL);
    SET(result, "radioGeneration", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.generation) : JS_NULL);
    SET(result, "nativeIdentity", dpp_identity(ctx, s->native.worker.native.identity));
    SET(result, "workerBusy", JS_NewBool(s->worker_busy));
    SET(result, "connectionRequested", JS_NewBool(s->connection_requested));
    SET(result, "connectionPrepared", JS_NewBool(s->connection_prepared));
    SET(result, "connectionVerified", JS_NewBool(s->connection_verified));
    SET(result, "connected", JS_NewBool(s->connected));
    SET(result, "configurationIndex", s->connection_requested ? JS_NewUint32(ctx, s->configuration_index) : JS_NULL);
    SET(result, "authentication", s->connection_requested ? JS_NewString(ctx,
        s->authentication == ESP32_MQUICKJS_DPP_AUTH_CONNECTOR ? "dpp" :
        s->authentication == ESP32_MQUICKJS_DPP_AUTH_SAE ? "wpa3-sae" : "wpa2-psk") : JS_NULL);
    SET(result, "connectionGeneration", s->connection_generation ? JS_NewUint32(ctx, s->connection_generation) : JS_NULL);
    SET(result, "connectionReason", s->connection_reason ? JS_NewInt32(ctx, s->connection_reason) : JS_NULL);
    SET(result, "stationConfigRestored", JS_NewBool(s->native.config_restored));
    SET(result, "storageRestored", JS_NewBool(s->native.storage_restored));
    SET(result, "restoreRequiresRestart", JS_NewBool(s->native.restore_requires_restart));
    SET(result, "restoreStopped", JS_NewBool(s->native.restore_stopped));
    SET(result, "restoreStarted", JS_NewBool(s->native.restore_started));
    SET(result, "restoreStartFailed", JS_NewBool(s->native.restore_start_failed));
    SET(result, "restoreStartError", s->native.restore_start_error ? JS_NewInt32(ctx, s->native.restore_start_error) : JS_NULL);
    SET(result, "recoveryPending", JS_NewBool(s->recovery_pending));
    SET(result, "recoveryAttempts", JS_NewUint32(ctx, s->native.restore_recovery_attempts));
    SET(result, "listening", JS_NewBool(s->listening));
    SET(result, "uriReady", JS_NewBool(s->uri_ready && !s->closing && !s->uri_consumed));
    SET(result, "uriConsumed", JS_NewBool(s->uri_consumed));
    SET(result, "configurationsReady", JS_NewBool(s->configs_ready && !s->closing && !s->configs_consumed));
    SET(result, "configurationsConsumed", JS_NewBool(s->configs_consumed));
    SET(result, "captureFinished", JS_NewBool(s->capture_finished));
    SET(result, "nativeClosed", JS_NewBool(s->native_closed));
    SET(result, "helperDrained", JS_NewBool(s->helper_drained));
    SET(result, "channelRestored", JS_NewBool(s->native.channel_restored));
    SET(result, "eventFenced", JS_NewBool(s->native.worker.native.event_fenced));
    SET(result, "closeRequested", JS_NewBool(s->closing));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "handoffUnknown", JS_NewBool(s->native.worker.handoff_unknown));
    SET(result, "terminalSeen", JS_NewBool(s->native.worker.native.terminal));
    SET(result, "configurationCount", JS_NewUint32(ctx, s->config_count));
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    SET(result, "observationDrops", JS_NewUint32(ctx, s->native.worker.native.observation_drops));
    SET(result, "asyncPending", JS_NewUint32(ctx, s->native.worker.native.async_pending));
    SET(result, "asyncActive", JS_NewUint32(ctx, s->native.worker.native.async_active));
    SET(result, "txWakeRetries", JS_NewUint32(ctx, s->native.worker.native.tx_wake_retries));
    SET(result, "txQueuedBytes", JS_NewUint32(ctx, s->native.worker.native.tx_queued_bytes));
    SET(result, "txBufferPresent", JS_NewBool(s->native.worker.native.tx_buffer_present));
    SET(result, "txRecycling", JS_NewBool(s->native.worker.native.tx_recycling));
    SET(result, "txRecycled", JS_NewBool(s->native.worker.native.tx_recycled));
    SET(result, "txQueued", JS_NewBool(s->native.worker.native.tx_queued));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(result, "txError", s->native.worker.native.tx_error ? JS_NewInt32(ctx, s->native.worker.native.tx_error) : JS_NULL);
    SET(result, "txBufferError", s->native.worker.native.tx_buffer_error ? JS_NewInt32(ctx, s->native.worker.native.tx_buffer_error) : JS_NULL);
    SET(result, "txPostError", s->native.worker.native.tx_post_error ? JS_NewInt32(ctx, s->native.worker.native.tx_post_error) : JS_NULL);
    SET(result, "channelTimerError", s->native.worker.native.chm_error ? JS_NewInt32(ctx, s->native.worker.native.chm_error) : JS_NULL);
    SET(result, "stage", s->stage ? JS_NewString(ctx, s->stage) : JS_NULL);
    SET(result, "cleanupStage", s->cleanup_stage ? JS_NewString(ctx, s->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
#define DPP_WATCH_HANDLES 4U
#define DPP_WATCH_CAPACITY 16U
typedef struct {
    uint32_t sequence;
    esp32_mquickjs_wifi_dpp_session_status_t status;
} dpp_watch_event_t;
typedef struct {
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_wifi_dpp_session_t *session;
    dpp_watch_event_t last;
    bool context_bound;
} dpp_watch_source_t;
static portMUX_TYPE s_dpp_watch_lock = portMUX_INITIALIZER_UNLOCKED;
static dpp_watch_source_t *s_dpp_watch_sources[DPP_WATCH_HANDLES];
static unsigned s_dpp_watch_handles;

/* Compare semantic fields, never padding or the transient worker-busy bit.
 * Every queued pointer is a framework stage string with static lifetime. */
static bool dpp_watch_same(const esp32_mquickjs_wifi_dpp_session_status_t *a,
    const esp32_mquickjs_wifi_dpp_session_status_t *b)
{
#define SAME(field) if (a->field != b->field) return false
    SAME(error);
    SAME(cleanup_error);
    SAME(stage);
    SAME(cleanup_stage);
    SAME(uri_ready);
    SAME(uri_consumed);
    SAME(configs_ready);
    SAME(configs_consumed);
    SAME(config_count);
    SAME(closing);
    SAME(retired);
    SAME(timed_out);
    SAME(capture_finished);
    SAME(native_closed);
    SAME(helper_drained);
    SAME(listening);
    SAME(connection_requested);
    SAME(connection_prepared);
    SAME(connection_verified);
    SAME(connected);
    SAME(configuration_index);
    SAME(connection_generation);
    SAME(connection_reason);
    SAME(authentication);
    SAME(native.config_restored);
    SAME(native.storage_restored);
    SAME(native.restore_requires_restart);
    SAME(native.restore_stopped);
    SAME(native.restore_started);
    SAME(native.restore_start_failed);
    SAME(native.restore_start_error);
    SAME(native.restore_recovery_attempts);
    SAME(recovery_pending);
    SAME(native.operation.identity);
    SAME(native.operation.generation);
    SAME(native.worker.native.identity);
    SAME(native.worker.handoff_unknown);
    SAME(native.channel_restored);
    SAME(native.worker.native.event_fenced);
    SAME(native.worker.native.terminal);
    SAME(native.worker.native.tx_error);
    SAME(native.worker.native.tx_buffer_error);
    SAME(native.worker.native.tx_post_error);
    SAME(native.worker.native.chm_error);
    SAME(native.worker.native.tx_buffer_present);
    SAME(native.worker.native.tx_recycling);
    SAME(native.worker.native.tx_recycled);
    SAME(native.worker.native.tx_queued);
#undef SAME
    return true;
}

static void dpp_watch_closed(void *opaque)
{
    dpp_watch_source_t *source = opaque;
    portENTER_CRITICAL(&s_dpp_watch_lock);
    for (unsigned i = 0; i < DPP_WATCH_HANDLES; ++i)
        if (s_dpp_watch_sources[i] == source) s_dpp_watch_sources[i] = NULL;
    esp32_mquickjs_wifi_dpp_session_t *session = source->session;
    source->session = NULL;
    bool bound = source->context_bound;
    if (!bound) --s_dpp_watch_handles;
    portEXIT_CRITICAL(&s_dpp_watch_lock);
    if (session) esp32_mquickjs_wifi_dpp_session_release(session);
    /* Only a private queue construction failure uses this path. No receiver
     * or producer has seen it; its deferred close owns the failed context. */
    if (!bound) esp32_mquickjs_memory_payload_free(source);
}

static void dpp_watch_destroyed(void *opaque)
{
    portENTER_CRITICAL(&s_dpp_watch_lock);
    --s_dpp_watch_handles;
    portEXIT_CRITICAL(&s_dpp_watch_lock);
    esp32_mquickjs_memory_payload_free(opaque); /* Budget survives close until queue storage dies. */
}

static JSValue dpp_watch_to_js(JSContext *ctx, const void *data, void *opaque)
{
    (void)opaque;
    const dpp_watch_event_t *event = data;
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, event->sequence));
    SET(result, "status", dpp_status_to_js(ctx, &event->status));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

bool esp32_mquickjs_wifi_dpp_poll_observations(bool close)
{
    bool handled = false;
    for (unsigned i = 0; i < DPP_WATCH_HANDLES; ++i) {
        esp32_mquickjs_event_queue_t *queue = NULL;
        esp32_mquickjs_wifi_dpp_session_t *session = NULL, *detached = NULL;
        portENTER_CRITICAL(&s_dpp_watch_lock);
        dpp_watch_source_t *source = s_dpp_watch_sources[i];
        if (source && esp32_mquickjs_event_queue_retain(source->queue)) {
            queue = source->queue;
            if (!close && esp32_mquickjs_wifi_dpp_session_retain(source->session)) session = source->session;
        } else if (source && close) {
            /* A disposed queue already belongs to its reaper. Detach now;
             * its later close callback sees NULL and cannot double-release. */
            s_dpp_watch_sources[i] = NULL;
            detached = source->session; source->session = NULL;
        }
        portEXIT_CRITICAL(&s_dpp_watch_lock);
        if (detached) esp32_mquickjs_wifi_dpp_session_release(detached);
        if (!queue) continue;
        if (close) {
            handled = esp32_mquickjs_event_queue_close(queue) || handled;
        } else if (session) {
            dpp_watch_event_t event = {0};
            if (esp32_mquickjs_wifi_dpp_session_observation(session, &event.status) &&
                (!source->last.sequence || !dpp_watch_same(&source->last.status, &event.status))) {
                if (source->last.sequence == UINT32_MAX) esp32_mquickjs_event_queue_request_close(queue);
                else {
                    event.sequence = source->last.sequence + 1U;
                    source->last = event; /* Drop-newest never retries an old snapshot. */
                    (void)esp32_mquickjs_event_queue_try_send_from_callback(queue, &event);
                    handled = true;
                }
            }
            esp32_mquickjs_wifi_dpp_session_release(session);
        }
        esp32_mquickjs_event_queue_release(queue);
    }
    return handled;
}

JSValue js_wifi_dpp_watch(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    esp32_mquickjs_wifi_dpp_session_t *session = dpp_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_dpp_session_retain(session))
        return JS_ThrowInternalError(ctx, "Dpp reference exhausted");
    JSGCRef options_ref, field_ref, queue_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *field = JS_PushGCRef(ctx, &field_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    uint32_t capacity = 8;
    if (argc > 1) goto invalid;
    *options = argc ? argv[0] : JS_UNDEFINED;
    if (!JS_IsUndefined(*options)) {
        static const char *const keys[] = {"capacity"};
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, "WiFiDppSession.watch", keys, 1)) goto fail;
        *field = JS_GetPropertyStr(ctx, *options, "capacity");
        if (JS_IsException(*field)) goto fail;
        if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, DPP_WATCH_CAPACITY, &capacity)) goto invalid;
    }
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (!runtime) { JS_ThrowInternalError(ctx, "Dpp watch requires an active runtime"); goto fail; }
    dpp_watch_source_t *source = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*source), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!source) { JS_ThrowOutOfMemory(ctx); goto fail; }
    unsigned slot = DPP_WATCH_HANDLES;
    bool duplicate = false;
    portENTER_CRITICAL(&s_dpp_watch_lock);
    for (unsigned i = 0; i < DPP_WATCH_HANDLES; ++i) {
        if (!s_dpp_watch_sources[i]) slot = i;
        else if (s_dpp_watch_sources[i]->session == session) duplicate = true;
    }
    bool available = !duplicate && slot != DPP_WATCH_HANDLES && s_dpp_watch_handles < DPP_WATCH_HANDLES;
    if (available) ++s_dpp_watch_handles;
    portEXIT_CRITICAL(&s_dpp_watch_lock);
    if (!available) {
        esp32_mquickjs_memory_payload_free(source); JS_ThrowInternalError(ctx, "Dpp watch already open or queue budget retained"); goto fail;
    }
    source->session = session; session = NULL; /* Context owns the retained reference. */
    *queue = esp32_mquickjs_event_queue_new_wireless("wifi", ctx, runtime, sizeof(dpp_watch_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, dpp_watch_to_js, NULL, dpp_watch_closed, source);
    if (JS_IsException(*queue)) { dpp_watch_closed(source); goto fail; }
    source->queue = esp32_mquickjs_event_queue_from_value(ctx, *queue);
    source->context_bound = esp32_mquickjs_event_queue_bind_context_release(source->queue, dpp_watch_destroyed);
    if (!source->context_bound) {
        /* Dispose owns deferred close; do not free its opaque context here. */
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
        JS_ThrowInternalError(ctx, "Dpp queue lifetime binding failed"); goto fail;
    }
    portENTER_CRITICAL(&s_dpp_watch_lock);
    s_dpp_watch_sources[slot] = source;
    portEXIT_CRITICAL(&s_dpp_watch_lock);
    JSValue result = JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref);
    return result;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Dpp watch capacity must be an integer in 1..16");
fail:
    if (session) esp32_mquickjs_wifi_dpp_session_release(session);
    JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}

static JSValue dpp_error(JSContext *ctx, const char *operation, esp32_mquickjs_wifi_dpp_session_t *session,
    esp_err_t error, bool wait_timeout)
{
    esp32_mquickjs_wifi_dpp_session_status_t status = {0};
    if (session) (void)esp32_mquickjs_wifi_dpp_session_status(session, &status);
    if (error == ESP_OK) error = status.error ? status.error : ESP_ERR_INVALID_STATE;
    bool timeout = wait_timeout || status.timed_out || error == ESP_ERR_TIMEOUT;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = dpp_status_to_js(ctx, &status);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(wait_timeout));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout ? "WIFI_DPP_TIMEOUT" :
        status.closing && status.error == ESP_OK ? "WIFI_DPP_CLOSED" : "WIFI_DPP_FAILED",
        operation, "Dpp operation did not complete; inspect session status", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_dpp_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "Dpp status expects no arguments");
    esp32_mquickjs_wifi_dpp_session_t *session = dpp_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_dpp_session_status_t status;
    (void)esp32_mquickjs_wifi_dpp_session_status(session, &status);
    return dpp_status_to_js(ctx, &status);
}
JSValue js_wifi_dpp_global_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "Dpp status expects no arguments");
    esp32_mquickjs_wifi_dpp_global_status_t status;
    esp32_mquickjs_wifi_dpp_global_status(&status);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "active", JS_NewBool(status.active));
    SET(result, "handles", JS_NewUint32(ctx, status.handles));
    SET(result, "workers", JS_NewUint32(ctx, status.workers));
    SET(result, "runtimeClosing", JS_NewBool(status.runtime_closing));
    SET(result, "session", status.active ? dpp_status_to_js(ctx, &status.session) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_dpp_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "Dpp capabilities expects no arguments");
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-dpp/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "enrollee", JS_TRUE);
    SET(result, "qrCode", JS_TRUE);
    SET(result, "autoConnect", JS_FALSE);
    SET(result, "configurationSelection", JS_TRUE);
    SET(result, "connectorAuthentication", JS_TRUE);
    SET(result, "pskAuthentication", JS_TRUE);
    #if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    SET(result, "saeAuthentication", JS_TRUE);
#else
    SET(result, "saeAuthentication", JS_FALSE);
#endif
    SET(result, "connectionOwnership", JS_NewString(ctx, "session"));
    SET(result, "observations", JS_TRUE);
    SET(result, "maxChannels", JS_NewInt32(ctx, ESP_DPP_MAX_CHAN_COUNT));
    SET(result, "maxConfigurations", JS_NewInt32(ctx, ESP_DPP_MAX_CONFIG_COUNT));
    SET(result, "maxSessions", JS_NewInt32(ctx, ESP32_MQUICKJS_DPP_MAX_HANDLES));
    SET(result, "maxActiveSessions", JS_NewInt32(ctx, 1));
    SET(result, "maxWatchQueues", JS_NewInt32(ctx, DPP_WATCH_HANDLES));
    SET(result, "maxWatchCapacity", JS_NewInt32(ctx, DPP_WATCH_CAPACITY));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_dpp_start(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.dpp.startEnrollee expects options");
    JSGCRef options_ref, result_ref;
    JSValue *input = JS_PushGCRef(ctx, &options_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *input = argv[0];
    dpp_options_t options;
    esp32_mquickjs_wifi_dpp_session_t *session = NULL;
    if (!dpp_options(ctx, &options_ref, &options)) goto fail;
    esp_err_t error = esp32_mquickjs_wifi_dpp_session_create(&options.config,
        options.allow_ap_channel_change, options.timeout_ms, &session);
    esp32_mquickjs_wireless_secure_zero(&options, sizeof(options));
    if (error != ESP_OK) { (void)dpp_error(ctx, "wifi.dpp.startEnrollee", session, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_DPP_SESSION);
    if (JS_IsException(*result)) goto fail;
    JS_SetOpaque(ctx, *result, session);
    error = esp32_mquickjs_wifi_dpp_session_activate(session);
    if (error != ESP_OK) {
        JS_SetOpaque(ctx, *result, NULL);
        (void)dpp_error(ctx, "wifi.dpp.startEnrollee", session, error, false); goto fail;
    }
    /* No JS allocations after activation. Native worker owns all RF calls. */
    (void)esp32_mquickjs_wifi_dpp_service();
    JSValue value = JS_PopGCRef(ctx, &result_ref);
    JS_PopGCRef(ctx, &options_ref); return value;
fail:
    esp32_mquickjs_wifi_dpp_session_release(session);
    JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &options_ref); return JS_EXCEPTION;
}

static JSValue dpp_bytes(JSContext *ctx, const uint8_t *bytes, size_t length)
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
static JSValue dpp_uri_to_js(JSContext *ctx, esp32_mquickjs_wifi_dpp_session_t *session)
{
    char uri[ESP32QJS_DPP_URI_MAX + 1U] = {0};
    esp_err_t error = esp32_mquickjs_wifi_dpp_session_uri(session, uri, sizeof(uri), false);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    if (error != ESP_OK) { (void)dpp_error(ctx, "WiFiDppSession.receive", session, error, false); goto fail; }
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "type", JS_NewString(ctx, "uri"));
    SET(result, "uri", JS_NewString(ctx, uri));
    error = esp32_mquickjs_wifi_dpp_session_uri(session, NULL, 0, true);
    if (error != ESP_OK) { (void)dpp_error(ctx, "WiFiDppSession.receive", session, error, false); goto fail; }
    esp32_mquickjs_wireless_secure_zero(uri, sizeof(uri));
    return JS_PopGCRef(ctx, &ref);
fail:
    esp32_mquickjs_wireless_secure_zero(uri, sizeof(uri));
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue dpp_configurations_to_js(JSContext *ctx, esp32_mquickjs_wifi_dpp_session_t *session)
{
    static const char *const akms[] = {"unknown", "dpp", "psk", "sae", "psk-sae", "sae-dpp", "psk-sae-dpp"};
    esp32_mquickjs_wifi_dpp_session_status_t status;
    (void)esp32_mquickjs_wifi_dpp_session_status(session, &status);
    esp_dpp_config_data_t row = {0};
    JSGCRef ref, entries_ref, entry_ref;
    JSValue *result = JS_PushGCRef(ctx, &ref), *entries = JS_PushGCRef(ctx, &entries_ref);
    JSValue *entry = JS_PushGCRef(ctx, &entry_ref);
    if (!status.configs_ready || !status.config_count || status.config_count > ESP_DPP_MAX_CONFIG_COUNT) goto invalid;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "type", JS_NewString(ctx, "configurations"));
    *entries = JS_NewArray(ctx, 0);
    if (JS_IsException(*entries)) goto fail;
    for (unsigned i = 0; i < status.config_count; ++i) {
        esp_err_t error = esp32_mquickjs_wifi_dpp_session_config(session, i, &row);
        if (error != ESP_OK) { (void)dpp_error(ctx, "WiFiDppSession.receive", session, error, false); goto fail; }
        if (!row.ssid_len || row.ssid_len > sizeof(row.ssid) || row.password_len > sizeof(row.password) ||
            row.connector_len >= sizeof(row.connector) || row.net_access_key_len > sizeof(row.net_access_key) ||
            row.c_sign_key_len > sizeof(row.c_sign_key) || row.akm >= sizeof(akms) / sizeof(akms[0]) ||
            (row.connector_len && (row.connector[row.connector_len] || memchr(row.connector, 0, row.connector_len)))) goto invalid;
        *entry = JS_NewObject(ctx);
        if (JS_IsException(*entry)) goto fail;
        SET(entry, "index", JS_NewUint32(ctx, i));
        SET(entry, "ssidBytes", dpp_bytes(ctx, row.ssid, row.ssid_len));
        SET(entry, "passwordBytes", dpp_bytes(ctx, row.password, row.password_len));
        SET(entry, "akm", JS_NewString(ctx, akms[row.akm]));
        SET(entry, "connector", row.connector_len ? JS_NewStringLen(ctx, row.connector, row.connector_len) : JS_NULL);
        SET(entry, "netAccessKeyBytes", dpp_bytes(ctx, row.net_access_key, row.net_access_key_len));
        SET(entry, "cSignKeyBytes", dpp_bytes(ctx, row.c_sign_key, row.c_sign_key_len));
        SET(entry, "netAccessKeyExpiry", dpp_identity(ctx, row.net_access_key_expiry));
        SET(entry, "channel", JS_NewUint32(ctx, row.curr_chan));
        if (JS_IsException(JS_SetPropertyUint32(ctx, *entries, i, *entry))) goto fail;
        esp32_mquickjs_wireless_secure_zero(&row, sizeof(row));
    }
    SET(result, "configurations", *entries);
    esp_err_t error = esp32_mquickjs_wifi_dpp_session_configs_commit(session);
    if (error != ESP_OK) { (void)dpp_error(ctx, "WiFiDppSession.receive", session, error, false); goto fail; }
    JS_PopGCRef(ctx, &entry_ref); JS_PopGCRef(ctx, &entries_ref); return JS_PopGCRef(ctx, &ref);
invalid:
    JS_ThrowInternalError(ctx, "invalid native DPP configuration");
fail:
    esp32_mquickjs_wireless_secure_zero(&row, sizeof(row));
    JS_PopGCRef(ctx, &entry_ref); JS_PopGCRef(ctx, &entries_ref); JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static const char *dpp_operation_name(dpp_operation_t operation)
{ return operation == DPP_RECEIVE ? "WiFiDppSession.receive" : operation == DPP_CONNECT ? "WiFiDppSession.connect" : "WiFiDppSession.close"; }
static void dpp_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state) return;
    if (state->observation_registered)
        esp32_mquickjs_wifi_dpp_session_wait_end(state->session, state->operation == DPP_CLOSE);
    esp32_mquickjs_wifi_dpp_session_release(state->session);
    esp32_mquickjs_memory_payload_free(state);
}
static bool dpp_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out, dpp_operation_t operation)
{
    *out = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "Dpp method expects optional timeout options"); return false; }
    esp32_mquickjs_wifi_dpp_session_t *session = dpp_receiver(ctx, self->val);
    if (!session) return false;
    if (!esp32_mquickjs_wifi_dpp_session_retain(session)) { JS_ThrowInternalError(ctx, "Dpp reference exhausted"); return false; }
    bool valid = true;
    uint32_t timeout = 1000;
    if (argc && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        valid = esp32_mquickjs_validate_plain_options(ctx, argv[0].val, dpp_operation_name(operation), keys, 1);
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
        esp32_mquickjs_wifi_dpp_session_release(session);
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid Dpp timeout options");
        return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { esp32_mquickjs_wifi_dpp_session_release(session); JS_ThrowOutOfMemory(ctx); return false; }
    state->session = session; state->operation = operation; state->timeout_ms = timeout;
    if (!esp32_mquickjs_wifi_dpp_session_wait_begin(session, operation == DPP_CLOSE)) {
        dpp_destroy(state); JS_ThrowInternalError(ctx, "Dpp waiter count exhausted"); return false;
    }
    state->observation_registered = true;
    *out = state; return true;
}
static bool dpp_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return dpp_capture(ctx, self, argc, argv, out, DPP_RECEIVE); }
static bool dpp_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return dpp_capture(ctx, self, argc, argv, out, DPP_CLOSE); }
static bool dpp_connect_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out)
{
    *out = NULL;
    uint32_t index, timeout = 30000;
    bool allow_ap_restart = false;
    esp32_mquickjs_wifi_dpp_auth_t auth = ESP32_MQUICKJS_DPP_AUTH_DEFAULT;
    if (argc < 1 || argc > 2 || !esp32_mquickjs_value_to_bounded_u32(ctx, argv[0].val, 0, ESP_DPP_MAX_CONFIG_COUNT - 1, &index))
        goto invalid;
    if (argc == 2 && !JS_IsUndefined(argv[1].val)) {
        static const char *const keys[] = {"authentication", "timeoutMs", "allowApRestart"};
        if (!esp32_mquickjs_validate_plain_options(ctx, argv[1].val, "WiFiDppSession.connect", keys, 3)) return false;
        JSGCRef ref;
        JSValue *field = JS_PushGCRef(ctx, &ref);
        *field = JS_GetPropertyStr(ctx, argv[1].val, "timeoutMs");
        bool ok = !JS_IsException(*field) && (JS_IsUndefined(*field) ||
            esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 3600000, &timeout));
        if (ok) {
            *field = JS_GetPropertyStr(ctx, argv[1].val, "authentication");
            ok = !JS_IsException(*field);
            if (ok && !JS_IsUndefined(*field)) {
                char name[16];
                ok = dpp_string(ctx, *field, name, sizeof(name));
                if (ok) {
                    if (!strcmp(name, "dpp")) auth = ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;
                    else if (!strcmp(name, "wpa2-psk")) auth = ESP32_MQUICKJS_DPP_AUTH_PSK;
                    else if (!strcmp(name, "wpa3-sae")) auth = ESP32_MQUICKJS_DPP_AUTH_SAE;
                    else ok = false;
                }
            }
        }
        if (ok) {
            *field = JS_GetPropertyStr(ctx, argv[1].val, "allowApRestart");
            ok = !JS_IsException(*field) && (JS_IsUndefined(*field) || JS_IsBool(*field));
            if (ok) allow_ap_restart = *field == JS_TRUE;
        }
        JS_PopGCRef(ctx, &ref);
        if (!ok) goto invalid;
    }
    if (!dpp_capture(ctx, self, 0, NULL, out, DPP_CONNECT)) return false;
    (*out)->timeout_ms = timeout; (*out)->configuration_index = index; (*out)->authentication = auth;
    (*out)->allow_ap_restart = allow_ap_restart;
    return true;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid DPP configuration index or connection options");
    return false;
}

static bool dpp_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)runtime; (void)token;
    int64_t now = esp_timer_get_time(), duration = (int64_t)state->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) { JS_ThrowInternalError(ctx, "Dpp clock exhausted"); return false; }
    if (state->operation == DPP_CONNECT) {
        esp_err_t error = esp32_mquickjs_wifi_dpp_session_connect(state->session, state->configuration_index,
            state->authentication, state->timeout_ms, state->allow_ap_restart);
        if (error != ESP_OK) { (void)dpp_error(ctx, "WiFiDppSession.connect", state->session, error, false); return false; }
    }
    state->started = true; state->deadline_us = now + duration;
    if (state->operation == DPP_CLOSE) esp32_mquickjs_wifi_dpp_session_close(state->session, false);
    (void)esp32_mquickjs_wifi_dpp_service();
    return true;
}
static esp32_mquickjs_future_poll_t dpp_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled) return ESP32_MQUICKJS_FUTURE_READY;
    (void)esp32_mquickjs_wifi_dpp_service();
    esp32_mquickjs_wifi_dpp_session_status_t status;
    (void)esp32_mquickjs_wifi_dpp_session_status(state->session, &status);
    bool ready = state->operation == DPP_CLOSE ? status.retired : state->operation == DPP_CONNECT ?
        status.connected || status.error || status.closing : status.error || status.closing ||
        status.configs_consumed || ((status.uri_ready || status.configs_ready) && !status.worker_busy);
    if (!ready && state->operation == DPP_RECEIVE && esp_timer_get_time() >= state->deadline_us) {
        state->wait_timed_out = true; ready = true;
    }
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue dpp_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->operation == DPP_CLOSE) return JS_UNDEFINED;
    if (state->wait_timed_out) return JS_NULL;
    esp32_mquickjs_wifi_dpp_session_status_t status;
    (void)esp32_mquickjs_wifi_dpp_session_status(state->session, &status);
    if (status.error || status.closing) return dpp_error(ctx, dpp_operation_name(state->operation), state->session, ESP_OK, false);
    if (state->operation == DPP_CONNECT) {
        esp32_mquickjs_wifi_link_snapshot_t link;
        double elapsed;
        esp_err_t error = esp32_mquickjs_wifi_dpp_session_connection_result(state->session, &link, &elapsed);
        if (error != ESP_OK) return dpp_error(ctx, "WiFiDppSession.connect", state->session, error, false);
        JSGCRef ref;
        JSValue *result = JS_PushGCRef(ctx, &ref);
        *result = esp32_mquickjs_wifi_make_connect_result(ctx, &link, elapsed);
        if (JS_IsException(*result) ||
            !esp32_mquickjs_set_property_ref(ctx, result, "configurationIndex", JS_NewUint32(ctx, status.configuration_index)) ||
            !esp32_mquickjs_set_property_ref(ctx, result, "connectionGeneration", JS_NewUint32(ctx, status.connection_generation)) ||
            !esp32_mquickjs_set_property_ref(ctx, result, "authentication", JS_NewString(ctx,
                status.authentication == ESP32_MQUICKJS_DPP_AUTH_CONNECTOR ? "dpp" :
                status.authentication == ESP32_MQUICKJS_DPP_AUTH_SAE ? "wpa3-sae" : "wpa2-psk"))) {
            JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
        }
        return JS_PopGCRef(ctx, &ref);
    }
    if (status.uri_ready && !status.uri_consumed) return dpp_uri_to_js(ctx, state->session);
    if (status.configs_consumed) return JS_ThrowReferenceError(ctx, "DPP configurations already consumed");
    return dpp_configurations_to_js(ctx, state->session);
}
static esp32_mquickjs_cancel_result_t dpp_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    if (state->operation == DPP_CONNECT && state->started) esp32_mquickjs_wifi_dpp_session_close(state->session, false);
    /* receive cancellation changes no native intent. Started close is already
     * retained by the registry; cancellation before start must not close. */
    return ESP32_MQUICKJS_CANCELLED;
}
static uint32_t dpp_timeout(const esp32_mquickjs_future_driver_state_t *state)
{ return state->operation == DPP_CLOSE ? state->timeout_ms : 0U; }
static JSValue dpp_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (state->operation == DPP_CONNECT && state->started) esp32_mquickjs_wifi_dpp_session_close(state->session, true);
    (void)dpp_cancel(state);
    return dpp_error(ctx, dpp_operation_name(state->operation), state->session, ESP_ERR_TIMEOUT, true);
}
#define DPP_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = dpp_start, .poll = dpp_poll, .finish = dpp_finish, \
    .cancel = dpp_cancel, .destroy = dpp_destroy, .timeout_ms = dpp_timeout, .on_timeout = dpp_on_timeout }
static const esp32_mquickjs_future_driver_t s_dpp_receive_driver = DPP_DRIVER(dpp_receive_capture);
static const esp32_mquickjs_future_driver_t s_dpp_close_driver = DPP_DRIVER(dpp_close_capture);
static const esp32_mquickjs_future_driver_t s_dpp_connect_driver = DPP_DRIVER(dpp_connect_capture);
static JSValue dpp_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver; *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref); return result;
}
JSValue js_wifi_dpp_recover(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "DPP recover expects no arguments");
    esp32_mquickjs_wifi_dpp_session_t *session = dpp_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp_err_t error = esp32_mquickjs_wifi_dpp_session_recover(session);
    if (error != ESP_OK) return dpp_error(ctx, "WiFiDppSession.recover", session, error, false);
    (void)esp32_mquickjs_wifi_dpp_service();
    return JS_UNDEFINED;
}
JSValue js_wifi_dpp_cancel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "DPP cancel expects no arguments");
    esp32_mquickjs_wifi_dpp_session_t *session = dpp_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_dpp_session_close(session, false);
    (void)esp32_mquickjs_wifi_dpp_service();
    return JS_UNDEFINED;
}
JSValue js_wifi_dpp_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return dpp_call(ctx, self, argc, argv, "receive"); }
JSValue js_wifi_dpp_connect(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return dpp_call(ctx, self, argc, argv, "connect"); }
JSValue js_wifi_dpp_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return dpp_call(ctx, self, argc, argv, "close"); }
bool esp32_mquickjs_init_wifi_dpp_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, proto_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *proto = JS_PushGCRef(ctx, &proto_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiDppSession");
    if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "receive");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_dpp_receive_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "connect");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_dpp_connect_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_dpp_close_driver)) goto done;
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register Dpp runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &proto_ref); JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
