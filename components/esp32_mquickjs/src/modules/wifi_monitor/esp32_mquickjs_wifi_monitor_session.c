#include "esp32_mquickjs_wifi_monitor_session.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_memory.h"
#include "esp_heap_caps.h"

static portMUX_TYPE s_monitor_sessions_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_monitor_session_t *s_monitor_sessions[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
static uint32_t s_monitor_next_generation = 1;

static bool monitor_session_retire_pool(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (!session->resources_initialized) return true;
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(&session->resources, &snapshot);
    if (!snapshot.initialized || esp32_mquickjs_wifi_monitor_resources_deinit(&session->resources)) return true;
    /* Another ref release may have completed retirement in the meantime. */
    esp32_mquickjs_wifi_monitor_resources_snapshot(&session->resources, &snapshot);
    return !snapshot.initialized;
}

bool esp32_mquickjs_wifi_monitor_session_retain(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (session == NULL) return false;
    uint32_t count = atomic_load_explicit(&session->references, memory_order_acquire);
    while (count != 0 && count != UINT32_MAX) {
        if (atomic_compare_exchange_weak_explicit(&session->references, &count, count + 1U,
                memory_order_acq_rel, memory_order_acquire)) return true;
    }
    return false;
}

void esp32_mquickjs_wifi_monitor_session_release(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (session == NULL) return;
    if (atomic_load_explicit(&session->closed, memory_order_acquire))
        (void)monitor_session_retire_pool(session);
    uint32_t count = atomic_load_explicit(&session->references, memory_order_acquire);
    while (count != 0) {
        if (!atomic_compare_exchange_weak_explicit(&session->references, &count, count - 1U,
                memory_order_acq_rel, memory_order_acquire)) continue;
        if (count != 1U) return;
        /* Missing native/Frame ownership is an invariant failure. Preserve the
         * bounded control instead of freeing referenced storage or runtime. */
        if (!atomic_load_explicit(&session->closed, memory_order_acquire) ||
            !monitor_session_retire_pool(session)) {
            atomic_store_explicit(&session->retirement_blocked, true, memory_order_release);
            atomic_store_explicit(&session->references, 1U, memory_order_release);
            return;
        }
        portENTER_CRITICAL(&s_monitor_sessions_lock);
        for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS; ++i)
            if (s_monitor_sessions[i] == session) s_monitor_sessions[i] = NULL;
        portEXIT_CRITICAL(&s_monitor_sessions_lock);
        esp32_mquickjs_memory_payload_free(session);
        return;
    }
}

static size_t monitor_sessions_snapshot(esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_wifi_monitor_session_t **sessions)
{
    size_t count = 0;
    portENTER_CRITICAL(&s_monitor_sessions_lock);
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS; ++i) {
        esp32_mquickjs_wifi_monitor_session_t *session = s_monitor_sessions[i];
        if (session != NULL && session->runtime == runtime && esp32_mquickjs_wifi_monitor_session_retain(session))
            sessions[count++] = session;
    }
    portEXIT_CRITICAL(&s_monitor_sessions_lock);
    return count;
}

size_t esp32_mquickjs_wifi_monitor_diagnostics_snapshot(
    esp32_mquickjs_wifi_monitor_diagnostic_t output[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS],
    uint32_t *unavailable)
{
    esp32_mquickjs_wifi_monitor_session_t *sessions[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
    size_t count = 0;
    *unavailable = 0;
    portENTER_CRITICAL(&s_monitor_sessions_lock);
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS; ++i) {
        esp32_mquickjs_wifi_monitor_session_t *session = s_monitor_sessions[i];
        if (session == NULL) continue;
        if (esp32_mquickjs_wifi_monitor_session_retain(session)) sessions[count++] = session;
        else ++*unavailable;
    }
    portEXIT_CRITICAL(&s_monitor_sessions_lock);
    for (size_t i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_monitor_session_t *session = sessions[i];
        output[i] = (esp32_mquickjs_wifi_monitor_diagnostic_t){
            .generation = session->generation,
            .closed = atomic_load_explicit(&session->closed, memory_order_acquire),
            .close_requested = atomic_load_explicit(&session->close_requested, memory_order_acquire),
            .retirement_blocked = atomic_load_explicit(&session->retirement_blocked, memory_order_acquire),
        };
        /* Never nest the registry and pool locks. A retained control remains
         * alive while a closed pool independently completes retirement. */
        esp32_mquickjs_wifi_monitor_resources_snapshot(&session->resources, &output[i].resources);
        esp32_mquickjs_wifi_monitor_session_release(session);
    }
    return count;
}

uint32_t esp32_mquickjs_wifi_monitor_reset_counters(void)
{
    esp32_mquickjs_wifi_monitor_session_t *sessions[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
    size_t count = 0;
    uint32_t unavailable = 0;
    portENTER_CRITICAL(&s_monitor_sessions_lock);
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS; ++i) {
        esp32_mquickjs_wifi_monitor_session_t *session = s_monitor_sessions[i];
        if (session == NULL) continue;
        if (esp32_mquickjs_wifi_monitor_session_retain(session)) sessions[count++] = session;
        else ++unavailable;
    }
    portEXIT_CRITICAL(&s_monitor_sessions_lock);
    for (size_t i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_monitor_resources_reset_counters(&sessions[i]->resources);
        esp32_mquickjs_wifi_monitor_session_release(sessions[i]);
    }
    return unavailable;
}

static void monitor_session_notify(void *opaque)
{
    esp32_mquickjs_wifi_monitor_session_t *session = opaque;
    /* Only the owning task or an admitted RX callback invokes this hook.
     * Runtime detaches after Radio drain; Frame/Source releases never notify. */
    if (session->runtime != NULL) esp32_mquickjs_notify_activity(session->runtime);
}

void esp32_mquickjs_wifi_monitor_session_request_close(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (session == NULL || atomic_load_explicit(&session->closed, memory_order_acquire)) return;
    atomic_store_explicit(&session->close_requested, true, memory_order_release);
    if (session->capture.initialized) esp32_mquickjs_wifi_monitor_capture_request_stop(&session->capture, true);
    else monitor_session_notify(session);
}

static bool monitor_session_on_task(esp32_mquickjs_wifi_monitor_session_t *session)
{
    return session != NULL && session->task == xTaskGetCurrentTaskHandle();
}

static esp_err_t monitor_session_cleanup(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (!monitor_session_on_task(session)) return ESP_ERR_INVALID_STATE;
    if (atomic_load_explicit(&session->closed, memory_order_acquire)) return ESP_OK;
    esp_err_t err = ESP_OK;
    if (session->capture.initialized) {
        err = esp32_mquickjs_wifi_monitor_capture_close(&session->capture);
        if (err != ESP_OK) return err;
    } else {
        if (session->resources_initialized)
            (void)esp32_mquickjs_wifi_monitor_resources_set_accepting(&session->resources, false);
        esp32_mquickjs_wifi_monitor_queue_detach(&session->bridge);
    }
    portENTER_CRITICAL(&s_monitor_sessions_lock);
    session->runtime = NULL;
    portEXIT_CRITICAL(&s_monitor_sessions_lock);
    /* closed=false and cleanup_hold prevent concurrent ref releases from
     * deinitializing this pool until its allocations have been classified. */
    (void)esp32_mquickjs_memory_wireless_retire(session->resources.slots);
    (void)esp32_mquickjs_memory_wireless_retire(session->resources.payload);
    atomic_store_explicit(&session->closed, true, memory_order_release);
    if (session->cleanup_hold) {
        session->cleanup_hold = false;
        esp32_mquickjs_wifi_monitor_session_release(session);
        /* Caller owns a separate temporary/JS/reaper reference. */
    }
    return ESP_OK;
}

static bool monitor_session_reap(void *opaque)
{
    esp32_mquickjs_wifi_monitor_session_t *session = opaque;
    if (!monitor_session_on_task(session)) return false;
    bool closing = atomic_load_explicit(&session->close_requested, memory_order_acquire);
    esp_err_t err = closing ? monitor_session_cleanup(session) :
        esp32_mquickjs_wifi_monitor_capture_stop(&session->capture);
    if (err != ESP_OK) return false;
    session->reaper_registered = false;
    session->reaper_full = false;
    esp32_mquickjs_wifi_monitor_session_release(session);
    return true;
}

static void monitor_session_schedule(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (session->reaper_registered || session->runtime == NULL ||
        !esp32_mquickjs_wifi_monitor_session_retain(session)) return;
    if (esp32_mquickjs_register_reaper(session->runtime, monitor_session_reap, session)) {
        session->reaper_registered = true;
        session->reaper_full = false;
    } else {
        session->reaper_full = true;
        esp32_mquickjs_wifi_monitor_session_release(session);
        /* cleanup_hold and the registry keep the Session reachable; poller and
         * explicit runtime teardown retry without allocating another owner. */
    }
}

static bool monitor_session_poll(JSContext *ctx, esp32_mquickjs_runtime_t *runtime, void *opaque)
{
    (void)ctx; (void)opaque;
    esp32_mquickjs_wifi_monitor_session_t *sessions[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
    size_t count = monitor_sessions_snapshot(runtime, sessions);
    bool handled = false;
    for (size_t i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_monitor_session_t *session = sessions[i];
        bool closing = atomic_load_explicit(&session->close_requested, memory_order_acquire);
        bool stopping = session->capture.initialized &&
            atomic_load_explicit(&session->capture.stop_requested, memory_order_acquire) &&
            session->capture.state != ESP32_MQUICKJS_WIFI_MONITOR_STOPPED;
        if ((closing || stopping) && !session->reaper_registered && monitor_session_on_task(session)) {
            monitor_session_schedule(session);
            handled = true;
        }
        esp32_mquickjs_wifi_monitor_session_release(session);
    }
    return handled;
}

esp_err_t esp32_mquickjs_wifi_monitor_session_start(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (!monitor_session_on_task(session) || session->reaper_registered ||
        atomic_load_explicit(&session->close_requested, memory_order_acquire) ||
        atomic_load_explicit(&session->closed, memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp32_mquickjs_wifi_monitor_capture_start(&session->capture);
    if (err != ESP_OK && session->capture.state == ESP32_MQUICKJS_WIFI_MONITOR_STOPPING)
        monitor_session_schedule(session);
    return err;
}

esp_err_t esp32_mquickjs_wifi_monitor_session_stop(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (!monitor_session_on_task(session)) return ESP_ERR_INVALID_STATE;
    if (atomic_load_explicit(&session->close_requested, memory_order_acquire))
        return esp32_mquickjs_wifi_monitor_session_close(session);
    esp_err_t err = esp32_mquickjs_wifi_monitor_capture_stop(&session->capture);
    if (err != ESP_OK) monitor_session_schedule(session);
    return err;
}

esp_err_t esp32_mquickjs_wifi_monitor_session_close(esp32_mquickjs_wifi_monitor_session_t *session)
{
    if (!monitor_session_on_task(session)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_monitor_session_request_close(session);
    esp_err_t err = monitor_session_cleanup(session);
    if (err != ESP_OK) monitor_session_schedule(session);
    return err;
}

static bool monitor_session_replace_ready(esp32_mquickjs_wifi_monitor_session_t *session)
{
    return monitor_session_on_task(session) && session->runtime != NULL &&
        !session->reaper_registered && !session->reaper_full &&
        !atomic_load_explicit(&session->close_requested, memory_order_acquire) &&
        !atomic_load_explicit(&session->closed, memory_order_acquire) &&
        session->capture.initialized && session->capture.state == ESP32_MQUICKJS_WIFI_MONITOR_STOPPED &&
        !session->capture.promiscuous.acquired && !session->capture.channel_claimed &&
        !atomic_load_explicit(&session->capture.close_requested, memory_order_acquire) &&
        session->bridge.queue != NULL && !esp32_mquickjs_event_queue_is_closed(session->bridge.queue);
}

esp_err_t esp32_mquickjs_wifi_monitor_session_replace(
    esp32_mquickjs_wifi_monitor_session_t *previous,
    esp32_mquickjs_wifi_monitor_session_t *replacement)
{
    if (previous == replacement || !monitor_session_replace_ready(previous) ||
        !monitor_session_replace_ready(replacement) || previous->runtime != replacement->runtime ||
        replacement->capture.radio.acquired || replacement->capture.radio_generation != 0)
        return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(&replacement->resources, &snapshot);
    if (!snapshot.initialized || snapshot.accepting || snapshot.identity_exhausted ||
        snapshot.counters.publishers != 0 || snapshot.counters.leased_frames != 0)
        return ESP_ERR_INVALID_STATE;
    /* No callback can observe either lease: both subscribers are already drained.
     * Keep Radio owned across reconfiguration; a buffer resize is not a driver
     * shutdown/restart and must not release another feature's ownership. */
    replacement->capture.radio = previous->capture.radio;
    previous->capture.radio = (esp32_mquickjs_wifi_radio_lease_t){0};
    replacement->capture.radio_generation = previous->capture.radio_generation;
    esp_err_t err = esp32_mquickjs_wifi_monitor_session_close(previous);
    if (err != ESP_OK) {
        /* Defensive invariant failure. Preserve exact lease ownership and the
         * previous Session's diagnostic close state; caller must not publish. */
        previous->capture.radio = replacement->capture.radio;
        replacement->capture.radio = (esp32_mquickjs_wifi_radio_lease_t){0};
        replacement->capture.radio_generation = 0;
        return err;
    }
    return ESP_OK;
}

bool esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(esp32_mquickjs_runtime_t *runtime)
{
    if (runtime == NULL) return false;
    esp32_mquickjs_wifi_monitor_session_t *sessions[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
    size_t count = monitor_sessions_snapshot(runtime, sessions);
    bool complete = true;
    for (size_t i = 0; i < count; ++i) {
        esp32_mquickjs_wifi_monitor_session_request_close(sessions[i]);
        if (esp32_mquickjs_wifi_monitor_session_close(sessions[i]) != ESP_OK) complete = false;
        esp32_mquickjs_wifi_monitor_session_release(sessions[i]);
    }
    return complete;
}

static void *monitor_session_calloc(size_t count, size_t size, void *opaque)
{
    (void)opaque;
    return esp32_mquickjs_memory_wireless_calloc("wifi.monitor", count, size,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
}
static void *monitor_session_malloc(size_t size, void *opaque)
{
    (void)opaque;
    return esp32_mquickjs_memory_wireless_alloc("wifi.monitor", size,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_POOL);
}
static void monitor_session_free(void *pointer, void *opaque)
{ (void)opaque; esp32_mquickjs_memory_payload_free(pointer); }
static bool monitor_session_context_retain(void *opaque)
{ return esp32_mquickjs_wifi_monitor_session_retain(opaque); }
static void monitor_session_context_release(void *opaque)
{ esp32_mquickjs_wifi_monitor_session_release(opaque); }
static void monitor_session_context_close(void *opaque)
{ esp32_mquickjs_wifi_monitor_session_request_close(opaque); }
static JSValue monitor_session_make_frame(JSContext *ctx,
    const esp32_mquickjs_wifi_monitor_event_t *event, void *opaque)
{
    esp32_mquickjs_wifi_monitor_session_t *session = opaque;
    return session->make_frame(ctx, event, session);
}

JSValue esp32_mquickjs_wifi_monitor_session_new(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    const esp32_mquickjs_wifi_monitor_capture_options_t *options,
    uint32_t pool_capacity, uint32_t snap_length, bool require_complete, uint32_t queue_capacity,
    esp32_mquickjs_wifi_monitor_make_frame_fn make_frame, esp32_mquickjs_wifi_monitor_session_t **output)
{
    size_t bytes;
    if (ctx == NULL) return JS_EXCEPTION;
    if (output == NULL || *output != NULL || runtime == NULL || options == NULL ||
        make_frame == NULL || !esp32_mquickjs_wifi_rx_filter_valid(&options->filter) ||
        !esp32_mquickjs_wifi_monitor_resources_size(pool_capacity, snap_length, &bytes) ||
        queue_capacity == 0 || queue_capacity > ESP32_MQUICKJS_NATIVE_POOL_MAX_CAPACITY)
        return JS_ThrowInternalError(ctx, "invalid Monitor Session configuration");
    /* Poller registration precedes allocations/Radio mutations and persists
     * until runtime disposal. It consumes one entry per runtime, not per Session. */
    if (!esp32_mquickjs_register_async_poller(runtime, monitor_session_poll, NULL))
        return JS_ThrowInternalError(ctx, "WIFI_MONITOR_RESOURCE_EXHAUSTED: poller capacity");
    esp32_mquickjs_wifi_monitor_session_t *session = esp32_mquickjs_memory_wireless_calloc(
        "wifi.monitor", 1, sizeof(*session), ESP32_MQUICKJS_MEMORY_DEFAULT,
        ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (session == NULL) return JS_ThrowOutOfMemory(ctx);
    session->runtime = runtime;
    session->task = xTaskGetCurrentTaskHandle();
    session->make_frame = make_frame;
    session->cleanup_hold = true;
    atomic_init(&session->references, 2U); /* caller + native cleanup */
    atomic_init(&session->closed, false);
    atomic_init(&session->close_requested, false);
    atomic_init(&session->retirement_blocked, false);
    portENTER_CRITICAL(&s_monitor_sessions_lock);
    for (size_t i = 0; i < ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS && s_monitor_next_generation != 0; ++i) {
        if (s_monitor_sessions[i] == NULL) {
            session->generation = s_monitor_next_generation;
            s_monitor_next_generation = s_monitor_next_generation == UINT32_MAX ? 0 : s_monitor_next_generation + 1U;
            s_monitor_sessions[i] = session;
            break;
        }
    }
    portEXIT_CRITICAL(&s_monitor_sessions_lock);
    if (session->generation == 0) {
        esp32_mquickjs_memory_payload_free(session);
        return JS_ThrowInternalError(ctx, "WIFI_MONITOR_RESOURCE_EXHAUSTED: Session capacity or generation");
    }
    session->bridge = (esp32_mquickjs_wifi_monitor_queue_t){
        .resources = &session->resources, .make_frame = monitor_session_make_frame,
        .retain_context = monitor_session_context_retain, .release_context = monitor_session_context_release,
        .request_close = monitor_session_context_close, .opaque = session,
    };
    esp32_mquickjs_wifi_monitor_allocator_t allocator = {
        monitor_session_calloc, monitor_session_malloc, monitor_session_free, NULL,
    };
    JSGCRef queue_ref;
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    *queue = JS_UNDEFINED;
    if (!esp32_mquickjs_wifi_monitor_resources_init(&session->resources, session->generation,
            pool_capacity, snap_length, require_complete, &allocator, esp32_mquickjs_wifi_monitor_queue_publish, &session->bridge)) {
        JS_ThrowOutOfMemory(ctx); goto fail;
    }
    session->resources_initialized = true;
    *queue = esp32_mquickjs_wifi_monitor_queue_new(ctx, runtime, &session->bridge, queue_capacity);
    if (JS_IsException(*queue)) goto fail;
    esp_err_t err = esp32_mquickjs_wifi_monitor_capture_init(&session->capture, &session->resources, &session->bridge, options);
    if (err != ESP_OK) { JS_ThrowInternalError(ctx, "Monitor capture initialization failed"); goto fail; }
    session->capture.notify_stop = monitor_session_notify;
    session->capture.notify_opaque = session;
    *output = session;
    return JS_PopGCRef(ctx, &queue_ref);
fail:
    (void)esp32_mquickjs_wifi_monitor_session_close(session);
    if (!JS_IsUndefined(*queue) && !JS_IsException(*queue))
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
    esp32_mquickjs_wifi_monitor_session_release(session);
    JS_PopGCRef(ctx, &queue_ref);
    return JS_EXCEPTION;
}
#endif
