#include "esp32_mquickjs_wifi_smartconfig.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp32_mquickjs_wifi_smartconfig_session.h"
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

typedef enum { SC_RECEIVE, SC_CLOSE } sc_operation_t;
typedef struct {
    esp32_mquickjs_wifi_smartconfig_decoder_options_t decoder;
    uint32_t timeout_ms;
    uint8_t key[16];
    bool allow_ap_channel_change, auto_connect;
    esp32_mquickjs_wifi_smartconfig_connection_options_t connection;
} sc_options_t;
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_wifi_smartconfig_session_t *session;
    sc_operation_t operation;
    uint32_t timeout_ms;
    int64_t deadline_us;
    bool started, cancelled, wait_timed_out, observation_registered;
};
#define SET(object, name, value) do { if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; } while (0)

static bool sc_options(JSContext *ctx, JSGCRef *root, sc_options_t *options)
{
    static const char *const keys[] = {"protocol", "fastMode", "channelTimeoutSeconds", "aesKey", "timeoutMs", "allowApChannelChange",
        "autoConnect", "connectionTimeoutMs", "allowOpenNetwork", "minimumAuthMode", "pmf"};
    static const char *const protocols[] = {"esptouch", "airkiss", "esptouch-airkiss", "esptouch-v2"};
    *options = (sc_options_t){.decoder = {.type = SC_TYPE_ESPTOUCH, .channel_timeout_s = 15}, .timeout_ms = 120000,
        .connection = {.timeout_ms = 30000, .minimum_auth = WIFI_AUTH_WPA2_PSK}};
    JSGCRef ref;
    JSValue *field = JS_PushGCRef(ctx, &ref);
    bool ok = false;
    if (!esp32_mquickjs_validate_plain_options(ctx, root->val, "wifi.smartConfig.start", keys, sizeof(keys) / sizeof(keys[0]))) goto done;
#define FIELD(name) do { *field = JS_GetPropertyStr(ctx, root->val, name); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("protocol");
    if (!JS_IsUndefined(*field)) {
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *field, protocols, 4, &index)) goto invalid;
        static const smartconfig_type_t types[] = {SC_TYPE_ESPTOUCH, SC_TYPE_AIRKISS, SC_TYPE_ESPTOUCH_AIRKISS, SC_TYPE_ESPTOUCH_V2};
        options->decoder.type = types[index];
    }
    FIELD("fastMode");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        options->decoder.fast_mode = (*field == JS_TRUE);
    }
    FIELD("allowApChannelChange");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        options->allow_ap_channel_change = (*field == JS_TRUE);
    }
    FIELD("channelTimeoutSeconds");
    if (!JS_IsUndefined(*field)) {
        uint32_t number;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 15, 255, &number)) goto invalid;
        options->decoder.channel_timeout_s = (uint8_t)number;
    }
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 3600000, &options->timeout_ms)) goto invalid;
    FIELD("autoConnect");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsBool(*field)) goto invalid;
        options->auto_connect = *field == JS_TRUE;
    }
    bool connection_settings = false;
    FIELD("connectionTimeoutMs");
    if (!JS_IsUndefined(*field)) {
        connection_settings = true;
        if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, 3600000, &options->connection.timeout_ms)) goto invalid;
    }
    FIELD("minimumAuthMode");
    if (!JS_IsUndefined(*field)) {
        connection_settings = true;
        const char *const names[] = {"wpa2-psk", "wpa3-psk"};
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *field, names, 2, &index)) goto invalid;
        options->connection.minimum_auth = index ? WIFI_AUTH_WPA3_PSK : WIFI_AUTH_WPA2_PSK;
        options->connection.pmf_required = index != 0;
    }
    FIELD("pmf");
    if (!JS_IsUndefined(*field)) {
        connection_settings = true;
        const char *const names[] = {"optional", "required"};
        size_t index;
        if (!esp32_mquickjs_value_to_enum(ctx, *field, names, 2, &index)) goto invalid;
        options->connection.pmf_required = index != 0;
    }
    FIELD("allowOpenNetwork");
    if (!JS_IsUndefined(*field)) {
        connection_settings = true;
        if (!JS_IsBool(*field)) goto invalid;
        options->connection.allow_open = *field == JS_TRUE;
    }
    if ((!options->auto_connect && connection_settings) || (options->auto_connect &&
        esp32_mquickjs_wifi_smartconfig_connection_validate_options(&options->connection) != ESP_OK)) goto invalid;
    FIELD("aesKey");
    if (!JS_IsUndefined(*field)) {
        if (!JS_IsString(ctx, *field)) goto invalid;
        JSCStringBuf buffer;
        size_t length = 0;
        const char *key = JS_ToCStringLen(ctx, &length, *field, &buffer);
        if (!key || length != sizeof(options->key) || memchr(key, 0, length)) goto invalid;
        memcpy(options->key, key, sizeof(options->key));
        options->decoder.key = options->key; options->decoder.key_length = sizeof(options->key);
    }
    if (esp32_mquickjs_wifi_smartconfig_decoder_validate_options(&options->decoder) != ESP_OK) goto invalid;
    ok = true; goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid SmartConfig options");
done:
    if (!ok) esp32_mquickjs_wireless_secure_zero(options, sizeof(*options));
    JS_PopGCRef(ctx, &ref); return ok;
#undef FIELD
}

static esp32_mquickjs_wifi_smartconfig_session_t *sc_receiver(JSContext *ctx, JSValue value)
{
    if (JS_GetClassID(ctx, value) != JS_CLASS_WIFI_SMARTCONFIG_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiSmartConfigSession"); return NULL;
    }
    esp32_mquickjs_wifi_smartconfig_session_t *session = JS_GetOpaque(ctx, value);
    if (!session) JS_ThrowReferenceError(ctx, "invalid WiFiSmartConfigSession");
    return session;
}
void js_wifi_smartconfig_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    /* Pending Future references keep the session alive. The registry requests
     * close when the final external reference, including those Futures, drops. */
    esp32_mquickjs_wifi_smartconfig_session_release(opaque);
}
JSValue js_wifi_smartconfig_constructor(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.smartConfig.start()");
}
static JSValue sc_identity(JSContext *ctx, uint64_t identity)
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
static JSValue sc_status_to_js(JSContext *ctx, const esp32_mquickjs_wifi_smartconfig_session_status_t *s)
{
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "state", JS_NewString(ctx, s->closing ? s->retired ? "closed" : "closing" :
        s->error ? "faulted" : s->completed ? "completed" :
        (s->auto_connect && s->credentials_ready) ? "connecting" : s->credentials_ready ? "credentials-ready" : s->native.decoder.started ? "listening" : "opening"));
    SET(result, "operation", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.identity) : JS_NULL);
    SET(result, "radioGeneration", s->native.operation.identity ? JS_NewUint32(ctx, s->native.operation.generation) : JS_NULL);
    SET(result, "decoderIdentity", sc_identity(ctx, s->native.decoder.token.identity));
    SET(result, "autoConnect", JS_NewBool(s->auto_connect));
    SET(result, "connectionGeneration", s->connection_generation ? JS_NewUint32(ctx, s->connection_generation) : JS_NULL);
    SET(result, "connectionStarted", JS_NewBool(s->connection_started));
    SET(result, "connectionTransferred", JS_NewBool(s->connection_transferred));
    SET(result, "connectionReason", JS_NewInt32(ctx, s->connection_reason));
    SET(result, "ackRequested", JS_NewBool(s->ack_requested));
    SET(result, "ackCompleted", JS_NewBool(s->connection_transferred || s->native.decoder.ack_completed));
    SET(result, "completed", JS_NewBool(s->completed));
    SET(result, "workerBusy", JS_NewBool(s->worker_busy));
    SET(result, "driverStarted", JS_NewBool(s->native.decoder.started));
    SET(result, "scanDone", JS_NewBool(s->native.events.scan_done));
    SET(result, "channelFound", JS_NewBool(s->native.events.channel_found));
    SET(result, "credentialsReady", JS_NewBool(s->credentials_ready && !s->closing && !s->credentials_consumed && (!s->auto_connect || s->completed)));
    SET(result, "credentialsConsumed", JS_NewBool(s->credentials_consumed));
    SET(result, "captureStopped", JS_NewBool(s->native.decoder.capture_stopped));
    SET(result, "channelRestored", JS_NewBool(s->native.channel_restored));
    SET(result, "eventFenced", JS_NewBool(s->native.event_fenced));
    SET(result, "homeChannel", s->native.home_primary ? JS_NewUint32(ctx, s->native.home_primary) : JS_NULL);
    SET(result, "closeRequested", JS_NewBool(s->closing));
    SET(result, "cleanupPending", JS_NewBool(s->closing && !s->retired));
    SET(result, "timedOut", JS_NewBool(s->timed_out));
    SET(result, "handoffUnknown", JS_NewBool(s->native.decoder.handoff_unknown));
    SET(result, "reservedBytes", JS_NewUint32(ctx, s->reserved_bytes));
    SET(result, "capturedEvents", JS_NewUint32(ctx, s->native.events.captured_events));
    SET(result, "duplicateCredentials", JS_NewUint32(ctx, s->native.events.duplicate_credentials));
    SET(result, "discardedEvents", JS_NewUint32(ctx, s->native.events.discarded_events));
    SET(result, "error", s->error ? JS_NewInt32(ctx, s->error) : JS_NULL);
    SET(result, "stage", s->stage ? JS_NewString(ctx, s->stage) : JS_NULL);
    SET(result, "cleanupError", s->cleanup_error ? JS_NewInt32(ctx, s->cleanup_error) : JS_NULL);
    SET(result, "cleanupStage", s->cleanup_stage ? JS_NewString(ctx, s->cleanup_stage) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
#define SC_WATCH_HANDLES 4U
#define SC_WATCH_CAPACITY 16U
typedef struct {
    uint32_t sequence;
    esp32_mquickjs_wifi_smartconfig_session_status_t status;
} sc_watch_event_t;
typedef struct {
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_wifi_smartconfig_session_t *session;
    sc_watch_event_t last;
    bool context_bound;
} sc_watch_source_t;
static portMUX_TYPE s_sc_watch_lock = portMUX_INITIALIZER_UNLOCKED;
static sc_watch_source_t *s_sc_watch_sources[SC_WATCH_HANDLES];
static unsigned s_sc_watch_handles;

/* Compare semantic fields, never padding or the transient worker-busy bit.
 * Every queued pointer is a framework stage string with static lifetime. */
static bool sc_watch_same(const esp32_mquickjs_wifi_smartconfig_session_status_t *a,
    const esp32_mquickjs_wifi_smartconfig_session_status_t *b)
{
#define SAME(field) if (a->field != b->field) return false
    SAME(error); SAME(cleanup_error); SAME(stage); SAME(cleanup_stage);
    SAME(credentials_ready); SAME(credentials_consumed); SAME(closing); SAME(retired); SAME(timed_out);
    SAME(auto_connect); SAME(connection_started); SAME(connection_transferred); SAME(completed);
    SAME(connection_generation); SAME(connection_reason); SAME(ack_requested);
    SAME(native.operation.identity); SAME(native.operation.generation);
    SAME(native.decoder.token.identity); SAME(native.decoder.started); SAME(native.decoder.capture_stopped);
    SAME(native.decoder.handoff_unknown); SAME(native.decoder.ack_completed);
    SAME(native.events.scan_done); SAME(native.events.channel_found);
    SAME(native.events.captured_events); SAME(native.events.duplicate_credentials); SAME(native.events.discarded_events);
    SAME(native.channel_restored); SAME(native.event_fenced); SAME(native.home_primary);
#undef SAME
    return true;
}

static void sc_watch_closed(void *opaque)
{
    sc_watch_source_t *source = opaque;
    portENTER_CRITICAL(&s_sc_watch_lock);
    for (unsigned i = 0; i < SC_WATCH_HANDLES; ++i)
        if (s_sc_watch_sources[i] == source) s_sc_watch_sources[i] = NULL;
    esp32_mquickjs_wifi_smartconfig_session_t *session = source->session;
    source->session = NULL;
    bool bound = source->context_bound;
    if (!bound) --s_sc_watch_handles;
    portEXIT_CRITICAL(&s_sc_watch_lock);
    if (session) esp32_mquickjs_wifi_smartconfig_session_release(session);
    /* Only a private queue construction failure uses this path. No receiver
     * or producer has seen it; its deferred close owns the failed context. */
    if (!bound) esp32_mquickjs_memory_payload_free(source);
}

static void sc_watch_destroyed(void *opaque)
{
    portENTER_CRITICAL(&s_sc_watch_lock);
    --s_sc_watch_handles;
    portEXIT_CRITICAL(&s_sc_watch_lock);
    esp32_mquickjs_memory_payload_free(opaque); /* Budget survives close until queue storage dies. */
}

static JSValue sc_watch_to_js(JSContext *ctx, const void *data, void *opaque)
{
    (void)opaque;
    const sc_watch_event_t *event = data;
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, event->sequence));
    SET(result, "status", sc_status_to_js(ctx, &event->status));
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

bool esp32_mquickjs_wifi_smartconfig_poll_observations(bool close)
{
    bool handled = false;
    for (unsigned i = 0; i < SC_WATCH_HANDLES; ++i) {
        esp32_mquickjs_event_queue_t *queue = NULL;
        esp32_mquickjs_wifi_smartconfig_session_t *session = NULL, *detached = NULL;
        portENTER_CRITICAL(&s_sc_watch_lock);
        sc_watch_source_t *source = s_sc_watch_sources[i];
        if (source && esp32_mquickjs_event_queue_retain(source->queue)) {
            queue = source->queue;
            if (!close && esp32_mquickjs_wifi_smartconfig_session_retain(source->session)) session = source->session;
        } else if (source && close) {
            /* A disposed queue already belongs to its reaper. Detach now;
             * its later close callback sees NULL and cannot double-release. */
            s_sc_watch_sources[i] = NULL;
            detached = source->session; source->session = NULL;
        }
        portEXIT_CRITICAL(&s_sc_watch_lock);
        if (detached) esp32_mquickjs_wifi_smartconfig_session_release(detached);
        if (!queue) continue;
        if (close) {
            handled = esp32_mquickjs_event_queue_close(queue) || handled;
        } else if (session) {
            sc_watch_event_t event = {0};
            if (esp32_mquickjs_wifi_smartconfig_session_observation(session, &event.status) &&
                (!source->last.sequence || !sc_watch_same(&source->last.status, &event.status))) {
                if (source->last.sequence == UINT32_MAX) esp32_mquickjs_event_queue_request_close(queue);
                else {
                    event.sequence = source->last.sequence + 1U;
                    source->last = event; /* Drop-newest never retries an old snapshot. */
                    (void)esp32_mquickjs_event_queue_try_send_from_callback(queue, &event);
                    handled = true;
                }
            }
            esp32_mquickjs_wifi_smartconfig_session_release(session);
        }
        esp32_mquickjs_event_queue_release(queue);
    }
    return handled;
}

JSValue js_wifi_smartconfig_watch(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    esp32_mquickjs_wifi_smartconfig_session_t *session = sc_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_smartconfig_session_retain(session))
        return JS_ThrowInternalError(ctx, "SmartConfig reference exhausted");
    JSGCRef options_ref, field_ref, queue_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *field = JS_PushGCRef(ctx, &field_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    uint32_t capacity = 8;
    if (argc > 1) goto invalid;
    *options = argc ? argv[0] : JS_UNDEFINED;
    if (!JS_IsUndefined(*options)) {
        static const char *const keys[] = {"capacity"};
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, "WiFiSmartConfigSession.watch", keys, 1)) goto fail;
        *field = JS_GetPropertyStr(ctx, *options, "capacity");
        if (JS_IsException(*field)) goto fail;
        if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, SC_WATCH_CAPACITY, &capacity)) goto invalid;
    }
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (!runtime) { JS_ThrowInternalError(ctx, "SmartConfig watch requires an active runtime"); goto fail; }
    sc_watch_source_t *source = esp32_mquickjs_memory_wireless_calloc("wifi", 1, sizeof(*source), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!source) { JS_ThrowOutOfMemory(ctx); goto fail; }
    unsigned slot = SC_WATCH_HANDLES;
    bool duplicate = false;
    portENTER_CRITICAL(&s_sc_watch_lock);
    for (unsigned i = 0; i < SC_WATCH_HANDLES; ++i) {
        if (!s_sc_watch_sources[i]) slot = i;
        else if (s_sc_watch_sources[i]->session == session) duplicate = true;
    }
    bool available = !duplicate && slot != SC_WATCH_HANDLES && s_sc_watch_handles < SC_WATCH_HANDLES;
    if (available) ++s_sc_watch_handles;
    portEXIT_CRITICAL(&s_sc_watch_lock);
    if (!available) {
        esp32_mquickjs_memory_payload_free(source); JS_ThrowInternalError(ctx, "SmartConfig watch already open or queue budget retained"); goto fail;
    }
    source->session = session; session = NULL; /* Context owns the retained reference. */
    *queue = esp32_mquickjs_event_queue_new_wireless("wifi", ctx, runtime, sizeof(sc_watch_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, sc_watch_to_js, NULL, sc_watch_closed, source);
    if (JS_IsException(*queue)) { sc_watch_closed(source); goto fail; }
    source->queue = esp32_mquickjs_event_queue_from_value(ctx, *queue);
    source->context_bound = esp32_mquickjs_event_queue_bind_context_release(source->queue, sc_watch_destroyed);
    if (!source->context_bound) {
        /* Dispose owns deferred close; do not free its opaque context here. */
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
        JS_ThrowInternalError(ctx, "SmartConfig queue lifetime binding failed"); goto fail;
    }
    portENTER_CRITICAL(&s_sc_watch_lock);
    s_sc_watch_sources[slot] = source;
    portEXIT_CRITICAL(&s_sc_watch_lock);
    JSValue result = JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref);
    return result;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "SmartConfig watch capacity must be an integer in 1..16");
fail:
    if (session) esp32_mquickjs_wifi_smartconfig_session_release(session);
    JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref);
    return JS_EXCEPTION;
}

static JSValue sc_error(JSContext *ctx, const char *operation, esp32_mquickjs_wifi_smartconfig_session_t *session,
    esp_err_t error, bool wait_timeout)
{
    esp32_mquickjs_wifi_smartconfig_session_status_t status = {0};
    if (session) (void)esp32_mquickjs_wifi_smartconfig_session_status(session, &status);
    if (error == ESP_OK) error = status.error ? status.error : ESP_ERR_INVALID_STATE;
    bool timeout = wait_timeout || status.timed_out || error == ESP_ERR_TIMEOUT;
    JSGCRef ref;
    JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = sc_status_to_js(ctx, &status);
    if (JS_IsException(*details)) goto fail;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    SET(details, "waitTimedOut", JS_NewBool(wait_timeout));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout ? "WIFI_SMARTCONFIG_TIMEOUT" :
        status.closing && status.error == ESP_OK ? "WIFI_SMARTCONFIG_CLOSED" : "WIFI_SMARTCONFIG_FAILED",
        operation, "SmartConfig operation did not complete; inspect session status", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_smartconfig_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "SmartConfig status expects no arguments");
    esp32_mquickjs_wifi_smartconfig_session_t *session = sc_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    esp32_mquickjs_wifi_smartconfig_session_status_t status;
    (void)esp32_mquickjs_wifi_smartconfig_session_status(session, &status);
    return sc_status_to_js(ctx, &status);
}
JSValue js_wifi_smartconfig_global_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "SmartConfig status expects no arguments");
    esp32_mquickjs_wifi_smartconfig_global_status_t status;
    esp32_mquickjs_wifi_smartconfig_global_status(&status);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "active", JS_NewBool(status.active));
    SET(result, "handles", JS_NewUint32(ctx, status.handles));
    SET(result, "workers", JS_NewUint32(ctx, status.workers));
    SET(result, "runtimeClosing", JS_NewBool(status.runtime_closing));
    SET(result, "session", status.active ? sc_status_to_js(ctx, &status.session) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_smartconfig_capabilities(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self; (void)argv;
    if (argc) return JS_ThrowTypeError(ctx, "SmartConfig capabilities expects no arguments");
    JSGCRef ref, protocols_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &ref), *protocols = JS_PushGCRef(ctx, &protocols_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    *protocols = JS_NewArray(ctx, 0);
    if (JS_IsException(*protocols)) goto fail;
    const char *const names[] = {"esptouch", "airkiss", "esptouch-airkiss", "esptouch-v2"};
    for (unsigned i = 0; i < 4; ++i) {
        *item = JS_NewString(ctx, names[i]);
        if (JS_IsException(*item) || JS_IsException(JS_SetPropertyUint32(ctx, *protocols, i, *item))) goto fail;
    }
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-smartconfig/1"));
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "version", JS_NewString(ctx, esp_smartconfig_get_version()));
    SET(result, "protocols", *protocols);
    SET(result, "espTouchV2", JS_TRUE);
    SET(result, "encryptedV2", JS_TRUE);
    SET(result, "autoConnect", JS_TRUE);
    SET(result, "acknowledgement", JS_TRUE);
    SET(result, "customData", JS_TRUE);
    SET(result, "observations", JS_TRUE);
    SET(result, "maxWatchQueues", JS_NewInt32(ctx, SC_WATCH_HANDLES));
    SET(result, "maxWatchCapacity", JS_NewInt32(ctx, SC_WATCH_CAPACITY));
    SET(result, "maxSessions", JS_NewUint32(ctx, ESP32_MQUICKJS_SMARTCONFIG_MAX_HANDLES));
    SET(result, "maxActiveSessions", JS_NewInt32(ctx, 1));
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &protocols_ref); return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &item_ref); JS_PopGCRef(ctx, &protocols_ref); JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
JSValue js_wifi_smartconfig_start(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    (void)self;
    if (argc != 1) return JS_ThrowTypeError(ctx, "wifi.smartConfig.start expects options");
    JSGCRef options_ref, result_ref;
    JSValue *input = JS_PushGCRef(ctx, &options_ref), *result = JS_PushGCRef(ctx, &result_ref);
    *input = argv[0];
    sc_options_t options;
    esp32_mquickjs_wifi_smartconfig_session_t *session = NULL;
    if (!sc_options(ctx, &options_ref, &options)) goto fail;
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_session_create(&options.decoder,
        options.allow_ap_channel_change, options.timeout_ms, &session);
    if (error == ESP_OK && options.auto_connect)
        error = esp32_mquickjs_wifi_smartconfig_session_configure_connection(session, &options.connection);
    esp32_mquickjs_wireless_secure_zero(&options, sizeof(options));
    if (error != ESP_OK) { (void)sc_error(ctx, "wifi.smartConfig.start", session, error, false); goto fail; }
    *result = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_SMARTCONFIG_SESSION);
    if (JS_IsException(*result)) goto fail;
    JS_SetOpaque(ctx, *result, session);
    error = esp32_mquickjs_wifi_smartconfig_session_activate(session);
    if (error != ESP_OK) {
        JS_SetOpaque(ctx, *result, NULL);
        (void)sc_error(ctx, "wifi.smartConfig.start", session, error, false); goto fail;
    }
    /* No JS allocations after activation. Native worker owns all RF calls. */
    (void)esp32_mquickjs_wifi_smartconfig_service();
    JSValue value = JS_PopGCRef(ctx, &result_ref);
    JS_PopGCRef(ctx, &options_ref); return value;
fail:
    esp32_mquickjs_wifi_smartconfig_session_release(session);
    JS_PopGCRef(ctx, &result_ref); JS_PopGCRef(ctx, &options_ref); return JS_EXCEPTION;
}

static JSValue sc_bytes(JSContext *ctx, const uint8_t *bytes, size_t length)
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
static JSValue sc_credentials_to_js(JSContext *ctx, esp32_mquickjs_wifi_smartconfig_session_t *session)
{
    esp32_mquickjs_wifi_smartconfig_credentials_t credentials = {0};
    esp_err_t error = esp32_mquickjs_wifi_smartconfig_session_credentials(session, &credentials, false);
    if (error != ESP_OK) return sc_error(ctx, "WiFiSmartConfigSession.receive", session, error, false);
    JSGCRef ref;
    JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    /* The SDK supplies fixed C-string fields, not lengths. Preserve all bytes
     * through the first NUL (or full capacity); never reinterpret as UTF-8. */
    SET(result, "ssidBytes", sc_bytes(ctx, credentials.network.ssid, strnlen((const char *)credentials.network.ssid, sizeof(credentials.network.ssid))));
    SET(result, "passwordBytes", sc_bytes(ctx, credentials.network.password, strnlen((const char *)credentials.network.password, sizeof(credentials.network.password))));
    const char *protocol = credentials.network.type == SC_TYPE_ESPTOUCH ? "esptouch" :
        credentials.network.type == SC_TYPE_AIRKISS ? "airkiss" : credentials.network.type == SC_TYPE_ESPTOUCH_V2 ? "esptouch-v2" : "esptouch-airkiss";
    SET(result, "protocol", JS_NewString(ctx, protocol));
    SET(result, "customDataBytes", credentials.network.type == SC_TYPE_ESPTOUCH_V2
        ? sc_bytes(ctx, credentials.custom_data, credentials.custom_length) : JS_NULL);
    char address[18];
    snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x", credentials.network.bssid[0],
        credentials.network.bssid[1], credentials.network.bssid[2], credentials.network.bssid[3], credentials.network.bssid[4], credentials.network.bssid[5]);
    SET(result, "bssid", credentials.network.bssid_set ? JS_NewString(ctx, address) : JS_NULL);
    error = esp32_mquickjs_wifi_smartconfig_session_credentials(session, NULL, true);
    if (error != ESP_OK) { (void)sc_error(ctx, "WiFiSmartConfigSession.receive", session, error, false); goto fail; }
    esp32_mquickjs_wireless_secure_zero(&credentials, sizeof(credentials));
    return JS_PopGCRef(ctx, &ref);
fail:
    esp32_mquickjs_wireless_secure_zero(&credentials, sizeof(credentials));
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static const char *sc_operation_name(sc_operation_t operation)
{ return operation == SC_RECEIVE ? "WiFiSmartConfigSession.receive" : "WiFiSmartConfigSession.close"; }
static void sc_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->observation_registered)
        esp32_mquickjs_wifi_smartconfig_session_wait_end(state->session, state->operation == SC_CLOSE);
    if (!state) return;
    esp32_mquickjs_wifi_smartconfig_session_release(state->session);
    esp32_mquickjs_memory_payload_free(state);
}
static bool sc_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out, sc_operation_t operation)
{
    *out = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "SmartConfig method expects optional timeout options"); return false; }
    esp32_mquickjs_wifi_smartconfig_session_t *session = sc_receiver(ctx, self->val);
    if (!session) return false;
    if (!esp32_mquickjs_wifi_smartconfig_session_retain(session)) { JS_ThrowInternalError(ctx, "SmartConfig reference exhausted"); return false; }
    bool valid = true;
    uint32_t timeout = 1000;
    if (argc && !JS_IsUndefined(argv[0].val)) {
        static const char *const keys[] = {"timeoutMs"};
        valid = esp32_mquickjs_validate_plain_options(ctx, argv[0].val, sc_operation_name(operation), keys, 1);
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
        esp32_mquickjs_wifi_smartconfig_session_release(session);
        if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid SmartConfig timeout options");
        return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc(
        "wireless.future", 1, sizeof(*state), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { esp32_mquickjs_wifi_smartconfig_session_release(session); JS_ThrowOutOfMemory(ctx); return false; }
    state->session = session; state->operation = operation; state->timeout_ms = timeout;
    if (!esp32_mquickjs_wifi_smartconfig_session_wait_begin(session, operation == SC_CLOSE)) {
        sc_destroy(state); JS_ThrowInternalError(ctx, "SmartConfig waiter count exhausted"); return false;
    }
    state->observation_registered = true;
    *out = state; return true;
}
static bool sc_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return sc_capture(ctx, self, argc, argv, out, SC_RECEIVE); }
static bool sc_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return sc_capture(ctx, self, argc, argv, out, SC_CLOSE); }
static bool sc_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)runtime; (void)token;
    int64_t now = esp_timer_get_time(), duration = (int64_t)state->timeout_ms * 1000;
    if (now < 0 || now > INT64_MAX - duration) { JS_ThrowInternalError(ctx, "SmartConfig clock exhausted"); return false; }
    state->started = true; state->deadline_us = now + duration;
    if (state->operation == SC_CLOSE) esp32_mquickjs_wifi_smartconfig_session_close(state->session, false);
    (void)esp32_mquickjs_wifi_smartconfig_service();
    return true;
}
static esp32_mquickjs_future_poll_t sc_poll(esp32_mquickjs_future_driver_state_t *state)
{
    if (state->cancelled) return ESP32_MQUICKJS_FUTURE_READY;
    (void)esp32_mquickjs_wifi_smartconfig_service();
    esp32_mquickjs_wifi_smartconfig_session_status_t status;
    (void)esp32_mquickjs_wifi_smartconfig_session_status(state->session, &status);
    bool ready = state->operation == SC_CLOSE ? status.retired : status.error || status.closing ||
        status.credentials_consumed || (status.credentials_ready && !status.worker_busy && (!status.auto_connect || status.completed));
    if (!ready && state->operation == SC_RECEIVE && esp_timer_get_time() >= state->deadline_us) {
        state->wait_timed_out = true; ready = true;
    }
    return ready ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
}
static JSValue sc_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->operation == SC_CLOSE) return JS_UNDEFINED;
    if (state->wait_timed_out) return JS_NULL;
    esp32_mquickjs_wifi_smartconfig_session_status_t status;
    (void)esp32_mquickjs_wifi_smartconfig_session_status(state->session, &status);
    if (status.error || status.closing) return sc_error(ctx, sc_operation_name(state->operation), state->session, ESP_OK, false);
    if (status.credentials_consumed) return JS_ThrowReferenceError(ctx, "SmartConfig credentials already consumed");
    return sc_credentials_to_js(ctx, state->session);
}
static esp32_mquickjs_cancel_result_t sc_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    /* receive cancellation changes no native intent. Started close is already
     * retained by the registry; cancellation before start must not close. */
    return ESP32_MQUICKJS_CANCELLED;
}
static uint32_t sc_timeout(const esp32_mquickjs_future_driver_state_t *state)
{ return state->operation == SC_RECEIVE ? 0U : state->timeout_ms; }
static JSValue sc_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms;
    (void)sc_cancel(state);
    return sc_error(ctx, sc_operation_name(state->operation), state->session, ESP_ERR_TIMEOUT, true);
}
#define SC_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = sc_start, .poll = sc_poll, .finish = sc_finish, \
    .cancel = sc_cancel, .destroy = sc_destroy, .timeout_ms = sc_timeout, .on_timeout = sc_on_timeout }
static const esp32_mquickjs_future_driver_t s_sc_receive_driver = SC_DRIVER(sc_receive_capture);
static const esp32_mquickjs_future_driver_t s_sc_close_driver = SC_DRIVER(sc_close_capture);
static JSValue sc_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver; *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref); return result;
}
JSValue js_wifi_smartconfig_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return sc_call(ctx, self, argc, argv, "receive"); }
JSValue js_wifi_smartconfig_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return sc_call(ctx, self, argc, argv, "close"); }
bool esp32_mquickjs_init_wifi_smartconfig_runtime(JSContext *ctx, esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef global_ref, object_ref, proto_ref, method_ref;
    JSValue *global = JS_PushGCRef(ctx, &global_ref), *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *proto = JS_PushGCRef(ctx, &proto_ref), *method = JS_PushGCRef(ctx, &method_ref);
    bool ok = false;
    *global = JS_GetGlobalObject(ctx);
    if (JS_IsException(*global)) goto done;
    *object = JS_GetPropertyStr(ctx, *global, "WiFiSmartConfigSession");
    if (JS_IsException(*object)) goto done;
    *proto = JS_GetPropertyStr(ctx, *object, "prototype");
    if (JS_IsException(*proto)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "receive");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_sc_receive_driver)) goto done;
    *method = JS_GetPropertyStr(ctx, *proto, "close");
    if (JS_IsException(*method) || !esp32_mquickjs_future_register_driver(ctx, runtime, *method, &s_sc_close_driver)) goto done;
    ok = true;
done:
    if (!ok && !JS_HasException(ctx)) JS_ThrowInternalError(ctx, "failed to register SmartConfig runtime");
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &proto_ref); JS_PopGCRef(ctx, &object_ref); JS_PopGCRef(ctx, &global_ref);
    return ok;
}
#endif
