#include "esp32_mquickjs_wifi_monitor.h"
#include "esp32_mquickjs_memory.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_options.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp32_mquickjs_wifi_monitor_options.h"
#include "esp32_mquickjs_wifi_monitor_wire.h"
#include "utils/esp32_mquickjs_byte_source.h"
#include "esp_heap_caps.h"
#include <string.h>

typedef struct {
    esp32_mquickjs_wifi_monitor_session_t *native;
    esp32_mquickjs_wifi_monitor_options_t options;
    unsigned queue_slot;
    bool configuring;
} monitor_session_t;
typedef struct {
    esp32_mquickjs_wifi_monitor_session_t *session;
    esp32_mquickjs_wifi_monitor_event_t event;
} monitor_frame_t;
typedef struct {
    esp32_mquickjs_wifi_monitor_session_t *session;
    uint32_t count, capacity;
    esp32_mquickjs_wifi_monitor_event_t events[];
} monitor_batch_t;
_Static_assert(ESP32_MQUICKJS_WIFI_MONITOR_MAX_BATCH_FRAMES <=
    (SIZE_MAX - sizeof(monitor_batch_t)) / sizeof(esp32_mquickjs_wifi_monitor_event_t),
    "Monitor Batch token allocation must fit size_t");
typedef struct {
    esp32_mquickjs_wifi_monitor_session_t *session;
    esp32_mquickjs_wifi_monitor_ref_t payload;
    size_t offset;
    bool opened, iterator_active, destroy_requested;
} monitor_reference_t;

#define SET(object, name, value) do { \
    if (!esp32_mquickjs_set_property_ref(ctx, object, name, value)) goto fail; \
} while (0)
#define NUMBER(object, name, value) SET(object, name, JS_NewFloat64(ctx, (double)(value)))

static monitor_session_t *monitor_session_from_this(JSContext *ctx, JSValue *value)
{
    if (value == NULL || JS_GetClassID(ctx, *value) != JS_CLASS_WIFI_MONITOR_SESSION) {
        JS_ThrowTypeError(ctx, "expected WiFiMonitorSession"); return NULL;
    }
    monitor_session_t *session = JS_GetOpaque(ctx, *value);
    if (session == NULL) JS_ThrowReferenceError(ctx, "invalid WiFiMonitorSession");
    return session;
}

static monitor_frame_t *monitor_frame_from_this(JSContext *ctx, JSValue *value, int argc)
{
    if (argc != 0 || value == NULL || JS_GetClassID(ctx, *value) != JS_CLASS_WIFI_MONITOR_FRAME) {
        JS_ThrowTypeError(ctx, "WiFiMonitorFrame method expects its Frame and no arguments"); return NULL;
    }
    monitor_frame_t *frame = JS_GetOpaque(ctx, *value);
    if (frame == NULL) JS_ThrowReferenceError(ctx, "WIFI_MONITOR_STALE_FRAME: frame is closed");
    return frame;
}

static JSValue monitor_error(JSContext *ctx, esp32_mquickjs_wifi_monitor_session_t *session,
    const char *operation, esp_err_t error)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    *details = JS_NewObject(ctx);
    if (JS_IsException(*details)) goto fail;
    NUMBER(details, "espCode", error);
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    const char *stage = session->capture.last_stage;
    const char *cleanup = session->capture.cleanup_stage;
    SET(details, "stage", stage != NULL ? JS_NewString(ctx, stage) : JS_NULL);
    SET(details, "cleanupStage", cleanup != NULL ? JS_NewString(ctx, cleanup) : JS_NULL);
    NUMBER(details, "cleanupEspCode", session->capture.cleanup_error);
    SET(details, "cleanupPending", JS_NewBool(session->reaper_registered || session->reaper_full ||
        session->capture.state == ESP32_MQUICKJS_WIFI_MONITOR_STOPPING));
    NUMBER(details, "generation", session->generation);
    (void)esp32_mquickjs_throw_native_error(ctx, "WIFI_MONITOR_FAILED", operation,
        "Wi-Fi Monitor operation failed; inspect details and wifi.status().radio", *details);
fail:
    JS_PopGCRef(ctx, &details_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_monitor_session_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "use wifi.monitor.open() to create a WiFiMonitorSession");
}
JSValue js_wifi_monitor_frame_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "receive a WiFiMonitorFrame from a Monitor session");
}

void js_wifi_monitor_session_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    monitor_session_t *session = opaque;
    if (session == NULL) return;
    esp32_mquickjs_wifi_monitor_session_request_close(session->native);
    esp32_mquickjs_wifi_monitor_session_release(session->native);
    esp32_mquickjs_memory_payload_free(session);
}
void js_wifi_monitor_frame_finalizer(JSContext *ctx, void *opaque)
{
    (void)ctx;
    monitor_frame_t *frame = opaque;
    if (frame == NULL) return;
    (void)esp32_mquickjs_wifi_monitor_close_frame(&frame->session->resources, &frame->event);
    esp32_mquickjs_wifi_monitor_session_release(frame->session);
    esp32_mquickjs_memory_payload_free(frame);
}

static JSValue monitor_make_frame(JSContext *ctx, const esp32_mquickjs_wifi_monitor_event_t *event,
    esp32_mquickjs_wifi_monitor_session_t *session)
{
    esp32_mquickjs_wifi_monitor_info_t snapshot;
    if (!esp32_mquickjs_wifi_monitor_frame_info(&session->resources, event, &snapshot))
        return JS_ThrowReferenceError(ctx, "WIFI_MONITOR_STALE_FRAME: invalid event owner");
    monitor_frame_t *frame = esp32_mquickjs_memory_wireless_calloc("wifi.monitor", 1, sizeof(*frame), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (frame == NULL) return JS_ThrowOutOfMemory(ctx);
    if (!esp32_mquickjs_wifi_monitor_session_retain(session)) {
        esp32_mquickjs_memory_payload_free(frame); return JS_ThrowInternalError(ctx, "Monitor context reference exhausted");
    }
    frame->session = session;
    frame->event = *event;
    JSGCRef object_ref, info_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *info = JS_PushGCRef(ctx, &info_ref);
    *info = JS_UNDEFINED;
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_MONITOR_FRAME);
    if (JS_IsException(*object)) goto fail;
    /* Ownership attaches only after every fallible JS allocation succeeds.
     * The queue bridge closes the PUBLIC root on any conversion failure. */
    *info = esp32_mquickjs_wifi_monitor_info_to_js(ctx, &snapshot, event->identity, session->capture.radio_generation);
    if (JS_IsException(*info)) goto fail;
    SET(object, "info", *info);
    JS_SetOpaque(ctx, *object, frame);
    JS_PopGCRef(ctx, &info_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    JS_PopGCRef(ctx, &info_ref);
    JS_PopGCRef(ctx, &object_ref);
    esp32_mquickjs_wifi_monitor_session_release(session);
    esp32_mquickjs_memory_payload_free(frame);
    return JS_EXCEPTION;
}

JSValue js_wifi_monitor_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    if (argc > 1) return JS_ThrowTypeError(ctx, "wifi.monitor.open expects options only");
    esp32_mquickjs_wifi_monitor_options_t options;
    if (!esp32_mquickjs_wifi_monitor_capture_options(ctx, argc == 0 ? JS_UNDEFINED : argv[0], &options))
        return JS_EXCEPTION;
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (runtime == NULL) return JS_ThrowInternalError(ctx, "Monitor requires an active runtime");
    monitor_session_t *session = NULL;
    JSGCRef object_ref, queue_ref, method_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    *queue = *method = JS_UNDEFINED;
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_MONITOR_SESSION);
    if (JS_IsException(*object)) goto fail;
    *method = JS_GetPropertyStr(ctx, *object, "receive");
    if (JS_IsException(*method)) goto fail;
    if (!esp32_mquickjs_event_queue_register_receive_alias(ctx, runtime, *method)) {
        if (!JS_HasException(ctx)) JS_ThrowInternalError(ctx, "Monitor receive Future registration failed");
        goto fail;
    }
    session = esp32_mquickjs_memory_wireless_calloc("wifi.monitor", 1, sizeof(*session), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (session == NULL) { JS_ThrowOutOfMemory(ctx); goto fail; }
    session->options = options;
    *queue = esp32_mquickjs_wifi_monitor_session_new(ctx, runtime, &options.capture,
        options.pool_capacity, options.snap_length, options.require_complete, options.queue_capacity,
        monitor_make_frame, &session->native);
    if (JS_IsException(*queue)) goto fail;
    SET(object, "_monitorQueue0", *queue);
    SET(object, "_monitorQueue1", JS_UNDEFINED);
    esp_err_t err = esp32_mquickjs_wifi_monitor_session_start(session->native);
    if (err != ESP_OK) { (void)monitor_error(ctx, session->native, "wifi.monitor.open", err); goto fail; }
    JS_SetOpaque(ctx, *object, session);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &queue_ref);
    return JS_PopGCRef(ctx, &object_ref);
fail:
    if (session != NULL && session->native != NULL)
        (void)esp32_mquickjs_wifi_monitor_session_close(session->native);
    if (!JS_IsUndefined(*queue) && !JS_IsException(*queue))
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
    if (session != NULL) {
        esp32_mquickjs_wifi_monitor_session_release(session->native);
        esp32_mquickjs_memory_payload_free(session);
    }
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_monitor_session_get_queue(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argc; (void)argv;
    monitor_session_t *session = monitor_session_from_this(ctx, this_val);
    if (session == NULL) return JS_EXCEPTION;
    return JS_GetPropertyStr(ctx, *this_val, session->queue_slot == 0 ? "_monitorQueue0" : "_monitorQueue1");
}

JSValue js_wifi_monitor_session_configure(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    if (argc != 1) return JS_ThrowTypeError(ctx, "Monitor configure expects one complete options object");
    monitor_session_t *session = monitor_session_from_this(ctx, this_val);
    if (session == NULL) return JS_EXCEPTION;
    if (session->configuring) return JS_ThrowTypeError(ctx, "Monitor configure is already active");
    session->configuring = true;
    esp32_mquickjs_wifi_monitor_options_t options;
    if (!esp32_mquickjs_wifi_monitor_capture_options(ctx, argv[0], &options)) {
        session->configuring = false; return JS_EXCEPTION;
    }
    esp32_mquickjs_wifi_monitor_session_t *previous = session->native;
    if (previous->capture.state != ESP32_MQUICKJS_WIFI_MONITOR_STOPPED || previous->reaper_registered ||
        previous->reaper_full || atomic_load_explicit(&previous->close_requested, memory_order_acquire)) {
        session->configuring = false;
        return monitor_error(ctx, previous, "WiFiMonitorSession.configure", ESP_ERR_INVALID_STATE);
    }
    esp32_mquickjs_wifi_monitor_session_t *replacement = NULL;
    JSGCRef object_ref, queue_ref, previous_queue_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    JSValue *previous_queue = JS_PushGCRef(ctx, &previous_queue_ref);
    *object = *this_val; *queue = *previous_queue = JS_UNDEFINED;
    *previous_queue = js_wifi_monitor_session_get_queue(ctx, object, 0, NULL);
    if (JS_IsException(*previous_queue)) goto fail;
    if (JS_GetClassID(ctx, *previous_queue) != JS_CLASS_EVENT_QUEUE ||
        JS_GetOpaque(ctx, *previous_queue) != previous->bridge.queue) {
        JS_ThrowReferenceError(ctx, "Monitor queue ownership was modified"); goto fail;
    }
    *queue = esp32_mquickjs_wifi_monitor_session_new(ctx, esp32_mquickjs_get_active_runtime(), &options.capture,
        options.pool_capacity, options.snap_length, options.require_complete, options.queue_capacity,
        monitor_make_frame, &replacement);
    if (JS_IsException(*queue)) goto fail;
    unsigned next_slot = session->queue_slot ^ 1U;
    /* Stage a strong JS queue owner in the inactive property before any native
     * ownership change. The getter selects only the committed slot; final commit
     * changes plain C state without JS allocation or a fallible property set. */
    SET(object, next_slot == 0 ? "_monitorQueue0" : "_monitorQueue1", *queue);
    esp_err_t err = esp32_mquickjs_wifi_monitor_session_replace(previous, replacement);
    if (err != ESP_OK) { (void)monitor_error(ctx, previous, "WiFiMonitorSession.configure", err); goto fail; }
    session->native = replacement;
    session->options = options;
    session->queue_slot = next_slot;
    session->configuring = false;
    (void)esp32_mquickjs_event_queue_dispose(ctx, *previous_queue);
    esp32_mquickjs_wifi_monitor_session_release(previous);
    JS_PopGCRef(ctx, &previous_queue_ref);
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &object_ref);
    /* Query is intentionally after commit, like wifi.configure. If this final
     * snapshot allocation fails, status still describes the committed config. */
    return js_wifi_monitor_session_status(ctx, this_val, 0, NULL);
fail:
    session->configuring = false;
    if (replacement != NULL) (void)esp32_mquickjs_wifi_monitor_session_close(replacement);
    if (!JS_IsUndefined(*queue) && !JS_IsException(*queue))
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
    esp32_mquickjs_wifi_monitor_session_release(replacement);
    JS_PopGCRef(ctx, &previous_queue_ref);
    JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &object_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_monitor_session_receive(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    if (monitor_session_from_this(ctx, this_val) == NULL) return JS_EXCEPTION;
    /* Same queue contract for direct receive and Future.call: a stopped source
     * may be awaited; successful close wakes it with null after queue discard. */
    JSGCRef method_ref;
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    *method = JS_GetPropertyStr(ctx, *this_val, "receive");
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(
        ctx, esp32_mquickjs_get_active_runtime(), *method, *this_val, argc, argv);
    JS_PopGCRef(ctx, &method_ref);
    return result;
}

static JSValue monitor_lifecycle(JSContext *ctx, JSValue *this_val, int argc, unsigned operation)
{
    if (argc != 0) return JS_ThrowTypeError(ctx, "Monitor lifecycle method expects no arguments");
    monitor_session_t *session = monitor_session_from_this(ctx, this_val);
    if (session == NULL) return JS_EXCEPTION;
    if (session->configuring) return JS_ThrowTypeError(ctx, "Monitor configure is active");
    esp_err_t err = operation == 0 ? esp32_mquickjs_wifi_monitor_session_start(session->native) :
        operation == 1 ? esp32_mquickjs_wifi_monitor_session_stop(session->native) :
        esp32_mquickjs_wifi_monitor_session_close(session->native);
    if (err != ESP_OK) return monitor_error(ctx, session->native,
        operation == 0 ? "WiFiMonitorSession.start" : operation == 1 ? "WiFiMonitorSession.stop" : "WiFiMonitorSession.close", err);
    /* Native detach closes/discards the queue. Keep the closed JS queue owned by
     * this Session until GC; its control reference no longer pins the pool. */
    return operation == 2 ? JS_UNDEFINED : js_wifi_monitor_session_status(ctx, this_val, 0, NULL);
}
JSValue js_wifi_monitor_session_start(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ (void)argv; return monitor_lifecycle(ctx, this_val, argc, 0); }
JSValue js_wifi_monitor_session_stop(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ (void)argv; return monitor_lifecycle(ctx, this_val, argc, 1); }
JSValue js_wifi_monitor_session_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{ (void)argv; return monitor_lifecycle(ctx, this_val, argc, 2); }

/* Read the native queue before allocating JS. Pool and queue observations have
 * independent locks and must not be presented as one atomic accounting sample. */
static JSValue monitor_queue_status(JSContext *ctx, esp32_mquickjs_event_queue_t *queue)
{
    esp32_mquickjs_event_queue_stats_t snapshot;
    if (queue == NULL || !esp32_mquickjs_event_queue_get_stats(queue, &snapshot)) return JS_NULL;
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "open", JS_NewBool(snapshot.open));
    NUMBER(result, "queued", snapshot.queued);
    NUMBER(result, "capacity", snapshot.capacity);
    NUMBER(result, "highWater", snapshot.high_water);
    SET(result, "receiverPending", JS_NewBool(snapshot.receiver_pending));
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}

JSValue esp32_mquickjs_wifi_monitor_diagnostics(JSContext *ctx)
{
    esp32_mquickjs_wifi_monitor_diagnostic_t snapshots[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
    uint32_t unavailable;
    size_t count = esp32_mquickjs_wifi_monitor_diagnostics_snapshot(snapshots, &unavailable);
    JSGCRef result_ref, sessions_ref, item_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *sessions = JS_PushGCRef(ctx, &sessions_ref);
    JSValue *item = JS_PushGCRef(ctx, &item_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    *sessions = JS_NewArray(ctx, 0);
    if (JS_IsException(*sessions)) goto fail;
    for (size_t i = 0; i < count; ++i) {
        const esp32_mquickjs_wifi_monitor_diagnostic_t *s = &snapshots[i];
        *item = JS_NewObject(ctx);
        if (JS_IsException(*item)) goto fail;
        NUMBER(item, "generation", s->generation);
        SET(item, "closed", JS_NewBool(s->closed));
        SET(item, "closeRequested", JS_NewBool(s->close_requested));
        SET(item, "retirementBlocked", JS_NewBool(s->retirement_blocked));
        SET(item, "accepting", JS_NewBool(s->resources.accepting));
        SET(item, "identityExhausted", JS_NewBool(s->resources.identity_exhausted));
        NUMBER(item, "controlBytes", sizeof(esp32_mquickjs_wifi_monitor_session_t));
        NUMBER(item, "poolBytes", s->resources.allocated_bytes);
        NUMBER(item, "freeSlots", s->resources.free_slots);
        NUMBER(item, "leasedFrames", s->resources.counters.leased_frames);
        NUMBER(item, "publishers", s->resources.counters.publishers);
        NUMBER(item, "callbacks", s->resources.counters.callbacks);
        NUMBER(item, "accepted", s->resources.counters.accepted);
        NUMBER(item, "droppedPoolFull", s->resources.counters.dropped_pool_full);
        NUMBER(item, "droppedQueueFull", s->resources.counters.dropped_queue_full);
        NUMBER(item, "droppedClosing", s->resources.counters.dropped_closing);
        if (JS_IsException(JS_SetPropertyUint32(ctx, *sessions, (uint32_t)i, *item))) goto fail;
    }
    SET(result, "sessions", *sessions);
    NUMBER(result, "unavailableSessions", unavailable);
    NUMBER(result, "capacity", ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS);
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &sessions_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &item_ref);
    JS_PopGCRef(ctx, &sessions_ref);
    JS_PopGCRef(ctx, &result_ref);
    return JS_EXCEPTION;
}

JSValue js_wifi_monitor_session_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "Monitor status expects no arguments");
    monitor_session_t *session = monitor_session_from_this(ctx, this_val);
    if (session == NULL) return JS_EXCEPTION;
    esp32_mquickjs_wifi_monitor_session_t *native = session->native;
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(&native->resources, &snapshot);
    JSGCRef result_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    static const char *const states[] = {"stopped", "starting", "running", "stopping", "closed"};
    SET(result, "state", JS_NewString(ctx, states[native->capture.state]));
    SET(result, "closeRequested", JS_NewBool(atomic_load_explicit(&native->close_requested, memory_order_acquire)));
    SET(result, "stopRequested", JS_NewBool(atomic_load_explicit(&native->capture.stop_requested, memory_order_acquire)));
    SET(result, "channelConflicted", JS_NewBool(atomic_load_explicit(&native->capture.channel_conflicted, memory_order_acquire)));
    SET(result, "cleanupPending", JS_NewBool(native->reaper_registered || native->reaper_full ||
        native->capture.state == ESP32_MQUICKJS_WIFI_MONITOR_STOPPING));
    SET(result, "retirementBlocked", JS_NewBool(atomic_load_explicit(&native->retirement_blocked, memory_order_acquire)));
    SET(result, "poolRetained", JS_NewBool(snapshot.initialized));
    SET(result, "accepting", JS_NewBool(snapshot.accepting));
    SET(result, "radioLeaseHeld", JS_NewBool(native->capture.radio.acquired));
    SET(result, "subscriberHeld", JS_NewBool(native->capture.promiscuous.acquired));
    SET(result, "fixedChannelHeld", JS_NewBool(native->capture.channel_claimed));
    SET(result, "queue", monitor_queue_status(ctx, native->bridge.queue));
    NUMBER(result, "generation", native->generation);
    NUMBER(result, "radioGeneration", native->capture.radio_generation);
    NUMBER(result, "requestedChannel", session->options.capture.channel);
    NUMBER(result, "startChannel", native->capture.effective_channel);
    NUMBER(result, "startChannelGeneration", native->capture.channel_generation);
    NUMBER(result, "poolCapacity", session->options.pool_capacity);
    NUMBER(result, "queueCapacity", session->options.queue_capacity);
    NUMBER(result, "snapLength", session->options.snap_length);
    SET(result, "requireComplete", JS_NewBool(session->options.require_complete));
    SET(result, "powerSavePolicy", JS_NewString(ctx, session->options.capture.require_power_save_none ? "require-none" : "preserve"));
    NUMBER(result, "allocatedPoolBytes", snapshot.allocated_bytes);
    NUMBER(result, "leasedFrames", snapshot.counters.leased_frames);
    NUMBER(result, "lastEspCode", native->capture.last_error);
    NUMBER(result, "cleanupEspCode", native->capture.cleanup_error);
    const char *stage = native->capture.last_stage, *cleanup = native->capture.cleanup_stage;
    SET(result, "lastStage", stage != NULL ? JS_NewString(ctx, stage) : JS_NULL);
    SET(result, "cleanupStage", cleanup != NULL ? JS_NewString(ctx, cleanup) : JS_NULL);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}

JSValue js_wifi_monitor_session_stats(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "Monitor stats expects no arguments");
    monitor_session_t *session = monitor_session_from_this(ctx, this_val);
    if (session == NULL) return JS_EXCEPTION;
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(&session->native->resources, &snapshot);
    esp32_mquickjs_wifi_monitor_counters_t *c = &snapshot.counters;
    JSGCRef result_ref, filtered_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *filtered = JS_PushGCRef(ctx, &filtered_ref);
    *filtered = JS_UNDEFINED;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    NUMBER(result, "callbacks", c->callbacks);
    NUMBER(result, "accepted", c->accepted);
    NUMBER(result, "droppedClosing", c->dropped_closing);
    NUMBER(result, "droppedPoolFull", c->dropped_pool_full);
    NUMBER(result, "droppedQueueFull", c->dropped_queue_full);
    NUMBER(result, "invalidCallbackData", c->invalid_callback_data);
    NUMBER(result, "droppedRequiredComplete", c->dropped_required_complete);
    NUMBER(result, "truncatedFrames", c->truncated_frames);
    NUMBER(result, "receivedBytes", c->received_bytes);
    NUMBER(result, "capturedBytes", c->captured_bytes);
    NUMBER(result, "droppedIdentityExhausted", c->dropped_identity_exhausted);
    NUMBER(result, "leasedFrames", c->leased_frames);
    NUMBER(result, "publishers", c->publishers);
    NUMBER(result, "freeSlots", snapshot.free_slots);
    SET(result, "identityExhausted", JS_NewBool(snapshot.identity_exhausted));
    *filtered = JS_NewObject(ctx);
    if (JS_IsException(*filtered)) goto fail;
    NUMBER(filtered, "invalidConfig", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG]);
    NUMBER(filtered, "invalidCallback", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CALLBACK]);
    NUMBER(filtered, "invalidHeader", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_HEADER]);
    NUMBER(filtered, "rxError", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_RX_ERROR]);
    NUMBER(filtered, "type", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE]);
    NUMBER(filtered, "subtype", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_SUBTYPE]);
    NUMBER(filtered, "mac", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_MAC]);
    NUMBER(filtered, "rssi", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_RSSI]);
    NUMBER(filtered, "decimation", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_DECIMATION]);
    NUMBER(filtered, "rate", c->filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_RATE]);
    SET(result, "filtered", *filtered);
    JS_PopGCRef(ctx, &filtered_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &filtered_ref);
    JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}

JSValue js_wifi_monitor_capabilities(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argv;
    if (argc != 0) return JS_ThrowTypeError(ctx, "Monitor capabilities expects no arguments");
    JSGCRef result_ref, child_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref);
    JSValue *child = JS_PushGCRef(ctx, &child_ref);
    *child = JS_UNDEFINED;
    *result = JS_NewObject(ctx);
    if (JS_IsException(*result)) goto fail;
    SET(result, "apiVersion", JS_NewString(ctx, "wifi-monitor/1"));
    SET(result, "available", JS_TRUE);
    SET(result, "stability", JS_NewString(ctx, "candidate"));
    SET(result, "target", JS_NewString(ctx, CONFIG_IDF_TARGET));
    SET(result, "idfVersion", JS_NewString(ctx, esp_get_idf_version()));
    SET(result, "timestampAccuracy", JS_NewString(ctx, "callback-time"));
    *child = JS_NewArray(ctx, 0);
    if (JS_IsException(*child)) goto fail;
    static const char *const frame_types[] = {"management", "control", "data", "misc"};
    for (unsigned i = 0; i < sizeof(frame_types) / sizeof(frame_types[0]); ++i) {
        JSValue value = JS_NewString(ctx, frame_types[i]);
        if (JS_IsException(value) || JS_IsException(JS_SetPropertyUint32(ctx, *child, i, value))) goto fail;
    }
    SET(result, "frameTypes", *child);
    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    SET(child, "receive", JS_TRUE);
    SET(child, "frameSource", JS_TRUE);
    SET(child, "receiveBatch", JS_TRUE);
    SET(child, "configure", JS_TRUE);
    SET(child, "fixedChannel", JS_TRUE);
    SET(child, "typeFilter", JS_TRUE);
    SET(child, "subtypeFilter", JS_TRUE);
    SET(child, "sourceMacFilter", JS_TRUE);
    SET(child, "destinationMacFilter", JS_TRUE);
    SET(child, "bssidFilter", JS_TRUE);
    SET(child, "rssiFilter", JS_TRUE);
    SET(child, "nativeDecimation", JS_TRUE);
    SET(child, "nativeRateLimit", JS_TRUE);
    SET(child, "wireSource", JS_TRUE);
    SET(child, "hostPcapngConverter", JS_TRUE);
    SET(result, "supports", *child);
    *child = JS_NewObject(ctx);
    if (JS_IsException(*child)) goto fail;
    NUMBER(child, "maxSessions", ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS);
    NUMBER(child, "maxPoolCapacity", ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY);
    NUMBER(child, "maxQueueCapacity", ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY);
    NUMBER(child, "maxSnapLength", ESP32_MQUICKJS_WIFI_MONITOR_MAX_SNAP_LENGTH);
    NUMBER(child, "maxBatchFrames", ESP32_MQUICKJS_WIFI_MONITOR_MAX_BATCH_FRAMES);
    NUMBER(child, "maxMacsPerRole", ESP32_MQUICKJS_WIFI_RX_FILTER_MAC_CAPACITY);
    SET(result, "limits", *child);
    JS_PopGCRef(ctx, &child_ref);
    return JS_PopGCRef(ctx, &result_ref);
fail:
    JS_PopGCRef(ctx, &child_ref);
    JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}

static void monitor_reference_release_payload(monitor_reference_t *reference)
{
    if (reference->session == NULL) return;
    (void)esp32_mquickjs_wifi_monitor_release_ref(&reference->payload);
    esp32_mquickjs_wifi_monitor_session_release(reference->session);
    reference->session = NULL;
}
static void monitor_view_release(void *opaque)
{
    monitor_reference_t *reference = opaque;
    if (reference == NULL) return;
    monitor_reference_release_payload(reference);
    esp32_mquickjs_memory_payload_free(reference);
}
static monitor_reference_t *monitor_frame_retain(JSContext *ctx, monitor_frame_t *frame)
{
    monitor_reference_t *reference = esp32_mquickjs_memory_wireless_calloc("wifi.monitor", 1, sizeof(*reference), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (reference == NULL) { JS_ThrowOutOfMemory(ctx); return NULL; }
    if (!esp32_mquickjs_wifi_monitor_session_retain(frame->session)) {
        esp32_mquickjs_memory_payload_free(reference); JS_ThrowInternalError(ctx, "Monitor context reference exhausted"); return NULL;
    }
    reference->session = frame->session;
    if (!esp32_mquickjs_wifi_monitor_retain_frame(&frame->session->resources, &frame->event, &reference->payload)) {
        monitor_view_release(reference);
        JS_ThrowReferenceError(ctx, "WIFI_MONITOR_STALE_FRAME: cannot retain frame"); return NULL;
    }
    return reference;
}

JSValue js_wifi_monitor_frame_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    monitor_frame_t *frame = monitor_frame_from_this(ctx, this_val, argc);
    if (frame == NULL) return JS_EXCEPTION;
    monitor_reference_t *reference = monitor_frame_retain(ctx, frame);
    if (reference == NULL) return JS_EXCEPTION;
    const uint8_t *data; size_t length;
    if (!esp32_mquickjs_wifi_monitor_ref_data(&reference->payload, &data, &length)) {
        monitor_view_release(reference); return JS_ThrowReferenceError(ctx, "Monitor bytes unavailable");
    }
    return esp32_mquickjs_new_wireless_retained_byte_view("wifi.monitor", ctx, data, length, monitor_view_release, reference);
}
JSValue js_wifi_monitor_frame_copy_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    monitor_frame_t *frame = monitor_frame_from_this(ctx, this_val, argc);
    if (frame == NULL) return JS_EXCEPTION;
    monitor_reference_t *reference = monitor_frame_retain(ctx, frame);
    if (reference == NULL) return JS_EXCEPTION;
    const uint8_t *data; size_t length;
    if (!esp32_mquickjs_wifi_monitor_ref_data(&reference->payload, &data, &length)) {
        monitor_view_release(reference); return JS_ThrowReferenceError(ctx, "Monitor bytes unavailable");
    }
    uint8_t *copy = length != 0 ? esp32_mquickjs_memory_wireless_alloc("wifi.monitor", length, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY) : NULL;
    if (length != 0 && copy == NULL) { monitor_view_release(reference); return JS_ThrowOutOfMemory(ctx); }
    if (length != 0) memcpy(copy, data, length);
    monitor_view_release(reference);
    return esp32_mquickjs_new_wireless_owned_byte_view("wifi.monitor", ctx, copy, length);
}
JSValue js_wifi_monitor_frame_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_MONITOR_FRAME)
        return JS_ThrowTypeError(ctx, "WiFiMonitorFrame.close expects its Frame and no arguments");
    monitor_frame_t *frame = JS_GetOpaque(ctx, *this_val);
    JS_SetOpaque(ctx, *this_val, NULL);
    js_wifi_monitor_frame_finalizer(ctx, frame);
    return JS_UNDEFINED;
}

static bool monitor_source_next(JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    (void)ctx;
    monitor_reference_t *reference = opaque;
    const uint8_t *data; size_t length;
    if (reference->session == NULL || !esp32_mquickjs_wifi_monitor_ref_data(&reference->payload, &data, &length) ||
        reference->offset >= length) return false;
    out->data = data + reference->offset;
    out->length = length - reference->offset;
    out->owner = JS_UNDEFINED;
    out->dma_capable = false;
    reference->offset = length;
    return true;
}
static void monitor_source_iterator_close(JSContext *ctx, void *opaque)
{
    (void)ctx;
    monitor_reference_t *reference = opaque;
    reference->iterator_active = false;
    monitor_reference_release_payload(reference);
    if (reference->destroy_requested) esp32_mquickjs_memory_payload_free(reference);
}
static bool monitor_source_open(JSContext *ctx, JSValue owner, void *opaque,
    esp32_mquickjs_byte_span_source_t *out, JSValue *out_error)
{
    (void)owner;
    monitor_reference_t *reference = opaque;
    if (reference->opened || reference->session == NULL) {
        *out_error = JS_ThrowReferenceError(ctx, "Monitor Source is closed or consumed"); return false;
    }
    reference->opened = true;
    reference->iterator_active = true;
    *out = (esp32_mquickjs_byte_span_source_t){reference, monitor_source_next, monitor_source_iterator_close};
    return true;
}
static size_t monitor_source_length(void *opaque)
{
    monitor_reference_t *reference = opaque;
    const uint8_t *data; size_t length;
    return reference->session != NULL && esp32_mquickjs_wifi_monitor_ref_data(&reference->payload, &data, &length) ? length : 0;
}
static void monitor_source_destroy(JSContext *ctx, void *opaque)
{
    (void)ctx;
    monitor_reference_t *reference = opaque;
    if (reference->iterator_active) reference->destroy_requested = true;
    else monitor_view_release(reference);
}
static const esp32_mquickjs_byte_span_source_object_ops_t monitor_source_ops = {
    .class_id = JS_CLASS_BYTE_SPAN_SOURCE, .open = monitor_source_open,
    .known_length = monitor_source_length, .destroy = monitor_source_destroy,
};
JSValue js_wifi_monitor_frame_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    monitor_frame_t *frame = monitor_frame_from_this(ctx, this_val, argc);
    if (frame == NULL) return JS_EXCEPTION;
    monitor_reference_t *reference = monitor_frame_retain(ctx, frame);
    if (reference == NULL) return JS_EXCEPTION;
    return esp32_mquickjs_new_wireless_byte_span_source("wifi.monitor", ctx, JS_UNDEFINED, &monitor_source_ops, reference);
}
/* Source owns one Session reference and one linear payload reference per frame.
 * No public Batch/Frame pointers survive creation or user option evaluation. */
typedef struct {
    esp32_mquickjs_wifi_monitor_session_t *session;
    uint8_t *control;
    uint32_t count, retained, index, control_length, total_length;
    uint8_t padding_pending;
    bool opened, iterator_active, destroy_requested, control_emitted;
    esp32_mquickjs_wifi_monitor_ref_t payloads[];
} monitor_wire_source_t;
_Static_assert(ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES <=
    (SIZE_MAX - sizeof(monitor_wire_source_t)) / sizeof(esp32_mquickjs_wifi_monitor_ref_t),
    "Monitor wire Source references must fit size_t");

static void monitor_wire_source_release(monitor_wire_source_t *source)
{
    for (uint32_t i = 0; i < source->retained; ++i)
        (void)esp32_mquickjs_wifi_monitor_release_ref(&source->payloads[i]);
    source->retained = 0;
    if (source->session != NULL) {
        esp32_mquickjs_wifi_monitor_session_release(source->session);
        source->session = NULL;
    }
    esp32_mquickjs_memory_payload_free(source->control);
    source->control = NULL;
}

/* Runtime-task-only: PUBLIC roots and metadata are stable throughout this
 * allocation-only helper. It never executes JS or accesses driver pointers. */
static monitor_wire_source_t *monitor_wire_source_create(
    esp32_mquickjs_wifi_monitor_session_t *session,
    const esp32_mquickjs_wifi_monitor_event_t *events, uint32_t count, bool *invalid)
{
    *invalid = true;
    if (session == NULL || events == NULL || count == 0 || count > ESP32_MQUICKJS_WIFI_RX_WIRE_MAX_FRAMES)
        return NULL;
    *invalid = false;
    monitor_wire_source_t *source = esp32_mquickjs_memory_wireless_calloc("wifi.monitor", 1, sizeof(*source) + (size_t)count * sizeof(source->payloads[0]), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (source == NULL) return NULL;
    esp32_mquickjs_wifi_rx_wire_frame_t *frames = esp32_mquickjs_memory_wireless_calloc("wifi.monitor", count, sizeof(*frames), ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (frames == NULL) goto fail;
    *invalid = true;
    if (!esp32_mquickjs_wifi_monitor_session_retain(session)) goto fail;
    source->session = session;
    source->count = count;
    esp32_mquickjs_wifi_monitor_info_t info;
    esp32_mquickjs_wifi_monitor_wire_snapshot_t snapshot;
    for (uint32_t i = 0; i < count; ++i) {
        if (!esp32_mquickjs_wifi_monitor_retain_frame(&session->resources, &events[i], &source->payloads[i])) goto fail;
        ++source->retained;
        const uint8_t *data; size_t length;
        if (!esp32_mquickjs_wifi_monitor_frame_info(&session->resources, &events[i], &info) ||
            !esp32_mquickjs_wifi_monitor_wire_snapshot(&info, events[i].identity, events[i].generation,
                session->capture.radio_generation, &snapshot) ||
            !esp32_mquickjs_wifi_monitor_ref_data(&source->payloads[i], &data, &length) ||
            length != snapshot.frame.packet_length) goto fail;
        frames[i] = snapshot.frame;
    }
    esp32_mquickjs_wifi_rx_wire_layout_t layout;
    if (!esp32_mquickjs_wifi_rx_wire_layout(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR, frames, count, &layout)) goto fail;
    source->control = esp32_mquickjs_memory_wireless_calloc("wifi.monitor", 1, layout.control_bytes, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (source->control == NULL) { *invalid = false; goto fail; }
    source->control_length = layout.control_bytes;
    source->total_length = layout.total_bytes;
    if (!esp32_mquickjs_wifi_rx_wire_write_control(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,
            frames, count, source->control, source->control_length)) goto fail;
    for (uint32_t i = 0; i < count; ++i) {
        if (!esp32_mquickjs_wifi_monitor_frame_info(&session->resources, &events[i], &info) ||
            !esp32_mquickjs_wifi_monitor_wire_snapshot(&info, events[i].identity, events[i].generation,
                session->capture.radio_generation, &snapshot) ||
            !esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,
                &frames[i], &snapshot.metadata,
                source->control + layout.metadata_base + (size_t)i * ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES,
                ESP32_MQUICKJS_WIFI_RX_WIRE_METADATA_BYTES)) goto fail;
    }
    esp32_mquickjs_memory_payload_free(frames);
    *invalid = false;
    return source;
fail:
    esp32_mquickjs_memory_payload_free(frames);
    monitor_wire_source_release(source);
    esp32_mquickjs_memory_payload_free(source);
    return NULL;
}

static bool monitor_wire_source_next(JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out)
{
    static const uint8_t padding[3] = {0};
    monitor_wire_source_t *source = opaque;
    if (source->session == NULL) return false;
    *out = (esp32_mquickjs_byte_span_t){.owner = JS_UNDEFINED, .dma_capable = false};
    if (!source->control_emitted) {
        source->control_emitted = true;
        out->data = source->control; out->length = source->control_length;
        return true;
    }
    if (source->padding_pending != 0) {
        out->data = padding; out->length = source->padding_pending;
        source->padding_pending = 0;
        return true;
    }
    while (source->index < source->count) {
        const uint8_t *data; size_t length;
        if (!esp32_mquickjs_wifi_monitor_ref_data(&source->payloads[source->index], &data, &length)) {
            JS_ThrowReferenceError(ctx, "WIFI_MONITOR_INVALID_DATA: retained Source payload unavailable"); return false;
        }
        ++source->index;
        if (length == 0) continue; /* Metadata-only record has no packet span. */
        out->data = data; out->length = length;
        source->padding_pending = (uint8_t)((4U - (length & 3U)) & 3U);
        return true;
    }
    return false;
}
static void monitor_wire_source_iterator_close(JSContext *ctx, void *opaque)
{
    (void)ctx;
    monitor_wire_source_t *source = opaque;
    source->iterator_active = false;
    monitor_wire_source_release(source);
    if (source->destroy_requested) esp32_mquickjs_memory_payload_free(source);
}
static bool monitor_wire_source_open(JSContext *ctx, JSValue owner, void *opaque,
    esp32_mquickjs_byte_span_source_t *out, JSValue *out_error)
{
    (void)owner;
    monitor_wire_source_t *source = opaque;
    if (source->opened || source->session == NULL) {
        *out_error = JS_ThrowReferenceError(ctx, "Monitor wire Source is closed or consumed"); return false;
    }
    source->opened = source->iterator_active = true;
    *out = (esp32_mquickjs_byte_span_source_t){source, monitor_wire_source_next, monitor_wire_source_iterator_close};
    return true;
}
static size_t monitor_wire_source_length(void *opaque)
{ return ((monitor_wire_source_t *)opaque)->total_length; }
static void monitor_wire_source_destroy(JSContext *ctx, void *opaque)
{
    (void)ctx;
    monitor_wire_source_t *source = opaque;
    if (source->iterator_active) source->destroy_requested = true;
    else { monitor_wire_source_release(source); esp32_mquickjs_memory_payload_free(source); }
}
static const esp32_mquickjs_byte_span_source_object_ops_t monitor_wire_source_ops = {
    .class_id = JS_CLASS_BYTE_SPAN_SOURCE, .open = monitor_wire_source_open,
    .known_length = monitor_wire_source_length, .destroy = monitor_wire_source_destroy,
};
JSValue js_wifi_monitor_batch_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    static const char *const allowed[] = {"format"};
    static const char *const formats[] = {"esp32qjs-monitor/1"};
    if (argc != 1 || this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_MONITOR_BATCH)
        return JS_ThrowTypeError(ctx, "WiFiMonitorBatch.source expects its Batch and {format: 'esp32qjs-monitor/1'}");
    if (!esp32_mquickjs_validate_plain_options(ctx, argv[0], "WiFiMonitorBatch.source", allowed, 1))
        return JS_EXCEPTION;
    JSValue format = JS_GetPropertyStr(ctx, argv[0], "format");
    if (JS_IsException(format)) return JS_EXCEPTION;
    size_t selected;
    if (!esp32_mquickjs_value_to_enum(ctx, format, formats, 1, &selected))
        return JS_ThrowTypeError(ctx, "Monitor batch source format must be esp32qjs-monitor/1");
    /* A getter may have closed the Batch. Resolve only after option capture. */
    monitor_batch_t *batch = JS_GetOpaque(ctx, *this_val);
    if (batch == NULL) return JS_ThrowReferenceError(ctx, "WIFI_MONITOR_STALE_BATCH: batch is closed");
    bool invalid;
    monitor_wire_source_t *source = monitor_wire_source_create(batch->session, batch->events, batch->count, &invalid);
    if (source == NULL) return invalid ?
        JS_ThrowInternalError(ctx, "WIFI_MONITOR_INVALID_DATA: cannot retain and encode batch") : JS_ThrowOutOfMemory(ctx);
    return esp32_mquickjs_new_wireless_byte_span_source("wifi.monitor", ctx, JS_UNDEFINED, &monitor_wire_source_ops, source);
}

JSValue js_wifi_monitor_batch_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "receive a WiFiMonitorBatch from a Monitor session");
}

static void monitor_batch_release(monitor_batch_t *batch)
{
    if (batch == NULL) return;
    for (uint32_t i = 0; i < batch->count; ++i)
        (void)esp32_mquickjs_wifi_monitor_close_frame(&batch->session->resources, &batch->events[i]);
    esp32_mquickjs_wifi_monitor_session_release(batch->session);
    esp32_mquickjs_memory_payload_free(batch);
}
void js_wifi_monitor_batch_finalizer(JSContext *ctx, void *opaque)
{ (void)ctx; monitor_batch_release(opaque); }

/* Transfers the Frame's existing PUBLIC root; it does not retain/copy payload.
 * Batch already owns an independent Session reference before any JS allocation. */
static bool monitor_batch_adopt_frame(JSContext *ctx, monitor_batch_t *batch, JSValue *value)
{
    if (JS_GetClassID(ctx, *value) != JS_CLASS_WIFI_MONITOR_FRAME) {
        JS_ThrowInternalError(ctx, "Monitor batch received an invalid Frame"); return false;
    }
    monitor_frame_t *frame = JS_GetOpaque(ctx, *value);
    if (frame == NULL || frame->session != batch->session || batch->count >= batch->capacity) {
        JS_ThrowReferenceError(ctx, "Monitor batch Frame belongs to another capture or is closed"); return false;
    }
    batch->events[batch->count++] = frame->event;
    JS_SetOpaque(ctx, *value, NULL);
    esp32_mquickjs_wifi_monitor_session_release(frame->session);
    esp32_mquickjs_memory_payload_free(frame);
    *value = JS_UNDEFINED;
    return true;
}

static bool monitor_batch_adopt_event(monitor_batch_t *batch,
    const esp32_mquickjs_wifi_monitor_event_t *event)
{
    if (batch->count >= batch->capacity ||
        !esp32_mquickjs_wifi_monitor_take_event(&batch->session->resources, event)) return false;
    batch->events[batch->count++] = *event;
    return true;
}

static bool monitor_batch_receiver_available(JSContext *ctx, esp32_mquickjs_event_queue_t *queue)
{
    esp32_mquickjs_event_queue_stats_t stats;
    if (!esp32_mquickjs_event_queue_get_stats(queue, &stats)) {
        JS_ThrowReferenceError(ctx, "Monitor receive queue is unavailable"); return false;
    }
    if (stats.receiver_pending) {
        JS_ThrowInternalError(ctx, "Monitor receive queue already has a pending receiver"); return false;
    }
    return true;
}

JSValue js_wifi_monitor_session_receive_batch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    if (argc > 1) return JS_ThrowTypeError(ctx, "Monitor receiveBatch expects at most one options argument");
    monitor_session_t *owner = monitor_session_from_this(ctx, this_val);
    if (owner == NULL) return JS_EXCEPTION;
    uint32_t generation = owner->native->generation;
    esp32_mquickjs_wifi_monitor_batch_options_t options;
    if (!esp32_mquickjs_wifi_monitor_capture_batch_options(ctx, argc == 0 ? JS_UNDEFINED : argv[0],
            owner->options.pool_capacity, &options)) return JS_EXCEPTION;
    if (owner->native->generation != generation)
        return JS_ThrowReferenceError(ctx, "Monitor configuration changed during batch option capture");
    /* Pin the original native owner and queue. Cooperative waits may run JS that
     * closes/reconfigures the public Session; a Batch never follows its new slot. */
    esp32_mquickjs_wifi_monitor_session_t *session = owner->native;
    if (atomic_load_explicit(&session->closed, memory_order_acquire)) return JS_NULL;
    esp32_mquickjs_event_queue_t *native_queue = session->bridge.queue;
    if (native_queue == NULL || !esp32_mquickjs_event_queue_retain(native_queue))
        return JS_ThrowReferenceError(ctx, "Monitor batch queue cannot be retained");
    if (!esp32_mquickjs_wifi_monitor_session_retain(session)) {
        esp32_mquickjs_event_queue_release(native_queue);
        return JS_ThrowInternalError(ctx, "Monitor context reference exhausted");
    }
    size_t bytes = sizeof(monitor_batch_t) + (size_t)options.maximum_frames * sizeof(esp32_mquickjs_wifi_monitor_event_t);
    monitor_batch_t *batch = esp32_mquickjs_memory_wireless_calloc("wifi.monitor", 1, bytes, ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (batch == NULL) {
        esp32_mquickjs_wifi_monitor_session_release(session);
        esp32_mquickjs_event_queue_release(native_queue);
        return JS_ThrowOutOfMemory(ctx);
    }
    batch->session = session;
    batch->capacity = options.maximum_frames;
    JSGCRef queue_ref, frame_ref, object_ref, argument_ref;
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    JSValue *frame = JS_PushGCRef(ctx, &frame_ref);
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *argument = JS_PushGCRef(ctx, &argument_ref);
    *queue = *frame = *object = *argument = JS_UNDEFINED;
    *queue = js_wifi_monitor_session_get_queue(ctx, this_val, 0, NULL);
    if (JS_IsException(*queue)) goto fail;
    if (JS_GetClassID(ctx, *queue) != JS_CLASS_EVENT_QUEUE || JS_GetOpaque(ctx, *queue) != native_queue) {
        JS_ThrowReferenceError(ctx, "Monitor batch queue ownership was modified"); goto fail;
    }
    if (!monitor_batch_receiver_available(ctx, native_queue)) goto fail;
    if (options.timeout_set) {
        *argument = JS_NewUint32(ctx, options.timeout_ms);
        if (JS_IsException(*argument)) goto fail;
    }
    *frame = js_event_queue_receive(ctx, queue, options.timeout_set ? 1 : 0, options.timeout_set ? argument : NULL);
    if (JS_IsException(*frame)) goto fail;
    if (JS_IsNull(*frame)) { *object = JS_NULL; goto done; }
    if (!monitor_batch_adopt_frame(ctx, batch, frame)) goto fail;
    int64_t deadline = esp_timer_get_time() + (int64_t)options.maximum_latency_ms * 1000;
    for (;;) {
        /* All native receive admission runs on this runtime task. Check before
         * raw queue draining so a competing receiver never loses its packet. */
        if (!monitor_batch_receiver_available(ctx, native_queue)) goto fail;
        while (batch->count < batch->capacity) {
            esp32_mquickjs_wifi_monitor_event_t event;
            if (!esp32_mquickjs_event_queue_try_receive(native_queue, &event)) break;
            if (!monitor_batch_adopt_event(batch, &event)) {
                (void)esp32_mquickjs_wifi_monitor_discard_event(&session->resources, &event);
                JS_ThrowReferenceError(ctx, "Monitor batch received a stale queue token"); goto fail;
            }
        }
        int64_t remaining = deadline - esp_timer_get_time();
        if (batch->count >= options.minimum_frames || batch->count >= batch->capacity ||
            options.maximum_latency_ms == 0 || remaining <= 0 || esp32_mquickjs_event_queue_is_closed(native_queue)) break;
        *argument = JS_NewUint32(ctx, (uint32_t)((remaining + 999) / 1000));
        if (JS_IsException(*argument)) goto fail;
        *frame = js_event_queue_receive(ctx, queue, 1, argument);
        if (JS_IsException(*frame)) goto fail;
        if (JS_IsNull(*frame)) { *frame = JS_UNDEFINED; break; }
        if (!monitor_batch_adopt_frame(ctx, batch, frame)) goto fail;
    }
    *object = JS_NewObjectClassUser(ctx, JS_CLASS_WIFI_MONITOR_BATCH);
    if (JS_IsException(*object)) goto fail;
    SET(object, "frameCount", JS_NewUint32(ctx, batch->count));
    JS_SetOpaque(ctx, *object, batch);
    batch = NULL;
done:
    monitor_batch_release(batch);
    esp32_mquickjs_event_queue_release(native_queue);
    JS_PopGCRef(ctx, &argument_ref);
    JSValue result = JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &frame_ref);
    JS_PopGCRef(ctx, &queue_ref);
    return result;
fail:
    if (!JS_IsException(*frame) && JS_GetClassID(ctx, *frame) == JS_CLASS_WIFI_MONITOR_FRAME)
        (void)js_wifi_monitor_frame_close(ctx, frame, 0, NULL);
    monitor_batch_release(batch);
    esp32_mquickjs_event_queue_release(native_queue);
    JS_PopGCRef(ctx, &argument_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &frame_ref);
    JS_PopGCRef(ctx, &queue_ref);
    return JS_EXCEPTION;
}

static monitor_batch_t *monitor_batch_from_this(JSContext *ctx, JSValue *value, int argc, JSValue *argv,
    uint32_t *index)
{
    if (value == NULL || JS_GetClassID(ctx, *value) != JS_CLASS_WIFI_MONITOR_BATCH || argc != 1) {
        JS_ThrowTypeError(ctx, "Monitor Batch method expects its Batch and one index"); return NULL;
    }
    monitor_batch_t *batch = JS_GetOpaque(ctx, *value);
    if (batch == NULL) { JS_ThrowReferenceError(ctx, "WIFI_MONITOR_STALE_BATCH: batch is closed"); return NULL; }
    if (!esp32_mquickjs_value_to_bounded_u32(ctx, argv[0], 0, batch->count - 1, index)) {
        if (!JS_HasException(ctx)) JS_ThrowRangeError(ctx, "Monitor Batch index is outside frameCount");
        return NULL;
    }
    return batch;
}
JSValue js_wifi_monitor_batch_info(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    uint32_t index;
    monitor_batch_t *batch = monitor_batch_from_this(ctx, this_val, argc, argv, &index);
    if (batch == NULL) return JS_EXCEPTION;
    esp32_mquickjs_wifi_monitor_info_t info;
    if (!esp32_mquickjs_wifi_monitor_frame_info(&batch->session->resources, &batch->events[index], &info))
        return JS_ThrowReferenceError(ctx, "WIFI_MONITOR_STALE_BATCH: frame owner is unavailable");
    return esp32_mquickjs_wifi_monitor_info_to_js(ctx, &info, batch->events[index].identity, batch->session->capture.radio_generation);
}
JSValue js_wifi_monitor_batch_bytes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    uint32_t index;
    monitor_batch_t *batch = monitor_batch_from_this(ctx, this_val, argc, argv, &index);
    if (batch == NULL) return JS_EXCEPTION;
    monitor_frame_t frame = {.session = batch->session, .event = batch->events[index]};
    monitor_reference_t *reference = monitor_frame_retain(ctx, &frame);
    if (reference == NULL) return JS_EXCEPTION;
    const uint8_t *data; size_t length;
    if (!esp32_mquickjs_wifi_monitor_ref_data(&reference->payload, &data, &length)) {
        monitor_view_release(reference); return JS_ThrowReferenceError(ctx, "Monitor Batch bytes unavailable");
    }
    return esp32_mquickjs_new_wireless_retained_byte_view("wifi.monitor", ctx, data, length, monitor_view_release, reference);
}
JSValue js_wifi_monitor_batch_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argv;
    if (argc != 0 || this_val == NULL || JS_GetClassID(ctx, *this_val) != JS_CLASS_WIFI_MONITOR_BATCH)
        return JS_ThrowTypeError(ctx, "Monitor Batch close expects its Batch and no arguments");
    monitor_batch_t *batch = JS_GetOpaque(ctx, *this_val);
    JS_SetOpaque(ctx, *this_val, NULL);
    monitor_batch_release(batch);
    return JS_UNDEFINED;
}

#undef SET
#undef NUMBER
#endif
