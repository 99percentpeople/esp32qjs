#include "esp32_mquickjs_wifi_monitor_queue.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO

static JSValue monitor_queue_convert(JSContext *ctx, const void *event, void *opaque)
{
    esp32_mquickjs_wifi_monitor_queue_t *bridge = opaque;
    if (!esp32_mquickjs_wifi_monitor_take_event(bridge->resources, event)) {
        return JS_ThrowReferenceError(ctx, "WIFI_MONITOR_STALE_FRAME: event owner is unavailable");
    }
    /* EventQueue marks the event finished before conversion. It will not drop
     * this root on an exception, so conversion failure is our responsibility. */
    JSValue result = bridge->make_frame(ctx, event, bridge->opaque);
    if (JS_IsException(result)) {
        (void)esp32_mquickjs_wifi_monitor_close_frame(bridge->resources, event);
    }
    return result;
}

static void monitor_queue_drop(void *event, void *opaque)
{
    esp32_mquickjs_wifi_monitor_queue_t *bridge = opaque;
    (void)esp32_mquickjs_wifi_monitor_discard_event(bridge->resources, event);
}

static void monitor_queue_close(void *opaque)
{
    esp32_mquickjs_wifi_monitor_queue_t *bridge = opaque;
    (void)esp32_mquickjs_wifi_monitor_resources_set_accepting(bridge->resources, false);
    bridge->request_close(bridge->opaque);
}

static void monitor_queue_release_context(void *opaque)
{
    esp32_mquickjs_wifi_monitor_queue_t *bridge = opaque;
    atomic_store_explicit(&bridge->context_owned, false, memory_order_release);
    bridge->release_context(bridge->opaque);
    /* Releasing the last reference may free bridge itself. */
}

JSValue esp32_mquickjs_wifi_monitor_queue_new(JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime, esp32_mquickjs_wifi_monitor_queue_t *bridge, uint32_t capacity)
{
    if (bridge == NULL || bridge->resources == NULL || bridge->queue != NULL ||
        atomic_load_explicit(&bridge->context_owned, memory_order_acquire) ||
        bridge->make_frame == NULL || bridge->retain_context == NULL ||
        bridge->release_context == NULL || bridge->request_close == NULL ||
        capacity == 0 || capacity > ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY) {
        return JS_ThrowInternalError(ctx, "invalid Monitor queue configuration");
    }
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(bridge->resources, &snapshot);
    if (!snapshot.initialized || snapshot.accepting || snapshot.counters.publishers != 0 ||
        snapshot.counters.leased_frames != 0) {
        return JS_ThrowInternalError(ctx, "Monitor queue requires an idle initialized pool");
    }
    if (!bridge->retain_context(bridge->opaque)) {
        return JS_ThrowInternalError(ctx, "WIFI_MONITOR_RESOURCE_EXHAUSTED: context retain failed");
    }
    JSGCRef value_ref;
    JSValue *value = JS_PushGCRef(ctx, &value_ref);
    *value = JS_UNDEFINED;
    *value = esp32_mquickjs_event_queue_new_wireless("wifi.monitor", ctx, runtime,
        sizeof(esp32_mquickjs_wifi_monitor_event_t), capacity,
        ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, monitor_queue_convert,
        monitor_queue_drop, monitor_queue_close, bridge);
    if (JS_IsException(*value)) {
        bridge->release_context(bridge->opaque);
        JS_PopGCRef(ctx, &value_ref);
        return JS_EXCEPTION;
    }
    esp32_mquickjs_event_queue_t *queue = esp32_mquickjs_event_queue_from_value(ctx, *value);
    if (queue == NULL || !esp32_mquickjs_event_queue_retain(queue)) {
        (void)esp32_mquickjs_event_queue_dispose(ctx, *value);
        bridge->release_context(bridge->opaque);
        JS_PopGCRef(ctx, &value_ref);
        return JS_ThrowInternalError(ctx, "WIFI_MONITOR_RESOURCE_EXHAUSTED: queue retain failed");
    }
    if (!esp32_mquickjs_event_queue_bind_context_release(queue, monitor_queue_release_context)) {
        (void)esp32_mquickjs_event_queue_dispose(ctx, *value);
        esp32_mquickjs_event_queue_release(queue);
        bridge->release_context(bridge->opaque);
        JS_PopGCRef(ctx, &value_ref);
        return JS_ThrowInternalError(ctx, "WIFI_MONITOR_RESOURCE_EXHAUSTED: queue context bind failed");
    }
    atomic_store_explicit(&bridge->context_owned, true, memory_order_release);
    bridge->queue = queue;
    return JS_PopGCRef(ctx, &value_ref);
}

bool esp32_mquickjs_wifi_monitor_queue_publish(
    const esp32_mquickjs_wifi_monitor_event_t *event, void *opaque)
{
    esp32_mquickjs_wifi_monitor_queue_t *bridge = opaque;
    return bridge != NULL && esp32_mquickjs_event_queue_try_send_from_callback(bridge->queue, event);
}

void esp32_mquickjs_wifi_monitor_queue_detach(esp32_mquickjs_wifi_monitor_queue_t *bridge)
{
    if (bridge == NULL || bridge->queue == NULL) return;
    esp32_mquickjs_event_queue_t *queue = bridge->queue;
    bridge->queue = NULL;
    (void)esp32_mquickjs_event_queue_close(queue);
    (void)esp32_mquickjs_event_queue_discard_all(queue);
    esp32_mquickjs_event_queue_release(queue);
}
#endif
