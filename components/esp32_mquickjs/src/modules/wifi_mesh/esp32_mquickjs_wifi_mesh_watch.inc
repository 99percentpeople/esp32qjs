#define MESH_WATCH_HANDLES 4U
#define MESH_WATCH_CAPACITY 16U
typedef struct {
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_wifi_mesh_session_t *session;
    uint32_t delivered_sequence;
    bool context_bound;
} mesh_watch_source_t;
static portMUX_TYPE s_mesh_watch_lock = portMUX_INITIALIZER_UNLOCKED;
static mesh_watch_source_t *s_mesh_watch_sources[MESH_WATCH_HANDLES];
static unsigned s_mesh_watch_handles;

static void mesh_watch_closed(void *opaque)
{
    mesh_watch_source_t *source = opaque;
    portENTER_CRITICAL(&s_mesh_watch_lock);
    for (unsigned i = 0; i < MESH_WATCH_HANDLES; ++i)
        if (s_mesh_watch_sources[i] == source) s_mesh_watch_sources[i] = NULL;
    esp32_mquickjs_wifi_mesh_session_t *session = source->session;
    source->session = NULL;
    bool bound = source->context_bound;
    if (!bound) --s_mesh_watch_handles;
    portEXIT_CRITICAL(&s_mesh_watch_lock);
    esp32_mquickjs_wifi_mesh_session_release(session);
    if (!bound) esp32_mquickjs_memory_payload_free(source);
}
static void mesh_watch_destroyed(void *opaque)
{
    portENTER_CRITICAL(&s_mesh_watch_lock); --s_mesh_watch_handles; portEXIT_CRITICAL(&s_mesh_watch_lock);
    esp32_mquickjs_memory_payload_free(opaque);
}
static JSValue mesh_watch_to_js(JSContext *ctx, const void *data, void *opaque)
{
    (void)opaque;
    const esp32_mquickjs_wifi_mesh_notice_t *event = data;
    JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, event->sequence));
    SET(result, "id", JS_NewInt32(ctx, event->id));
    SET(result, "fields", JS_NewUint32(ctx, event->fields));
    SET(result, "mac", event->fields & ESP32_MQUICKJS_MESH_EVENT_MAC ? mesh_address(ctx, event->mac, 6) : JS_NULL);
    SET(result, "channel", event->fields & ESP32_MQUICKJS_MESH_EVENT_CHANNEL ? JS_NewInt32(ctx, event->channel) : JS_NULL);
    SET(result, "layer", event->fields & ESP32_MQUICKJS_MESH_EVENT_LAYER ? JS_NewInt32(ctx, event->layer) : JS_NULL);
    SET(result, "reason", event->fields & ESP32_MQUICKJS_MESH_EVENT_REASON ? JS_NewInt32(ctx, event->reason) : JS_NULL);
    SET(result, "tableSize", event->fields & ESP32_MQUICKJS_MESH_EVENT_TABLE ? JS_NewInt32(ctx, event->table_size) : JS_NULL);
    SET(result, "tableChange", event->fields & ESP32_MQUICKJS_MESH_EVENT_TABLE ? JS_NewInt32(ctx, event->table_change) : JS_NULL);
    SET(result, "value", event->fields & ESP32_MQUICKJS_MESH_EVENT_BOOLEAN ? JS_NewBool(event->value) : JS_NULL);
    SET(result, "duty", event->fields & ESP32_MQUICKJS_MESH_EVENT_DUTY ? JS_NewInt32(ctx, event->duty) : JS_NULL);
    return JS_PopGCRef(ctx, &ref);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}
bool esp32_mquickjs_wifi_mesh_poll_observations(bool close)
{
    bool handled = false;
    for (unsigned i = 0; i < MESH_WATCH_HANDLES; ++i) {
        esp32_mquickjs_event_queue_t *queue = NULL;
        esp32_mquickjs_wifi_mesh_session_t *session = NULL, *detached = NULL;
        portENTER_CRITICAL(&s_mesh_watch_lock);
        mesh_watch_source_t *source = s_mesh_watch_sources[i];
        if (source && esp32_mquickjs_event_queue_retain(source->queue)) {
            queue = source->queue;
            if (!close && esp32_mquickjs_wifi_mesh_session_retain(source->session)) session = source->session;
        } else if (source && close) {
            s_mesh_watch_sources[i] = NULL; detached = source->session; source->session = NULL;
        }
        portEXIT_CRITICAL(&s_mesh_watch_lock);
        esp32_mquickjs_wifi_mesh_session_release(detached);
        if (!queue) continue;
        if (close) handled = esp32_mquickjs_event_queue_close(queue) || handled;
        else if (session) {
            for (unsigned j = 0; j < ESP32_MQUICKJS_MESH_EVENT_SLOTS; ++j) {
                esp32_mquickjs_wifi_mesh_notice_t event;
                if (esp32_mquickjs_wifi_mesh_event_copy(session, &event)) break;
                if (source->delivered_sequence != event.sequence) {
                    /* Queue saturation drops only observation. Remember even
                     * a dropped attempt so a failed try-lock commit cannot
                     * duplicate it in a later poll. */
                    source->delivered_sequence = event.sequence;
                    (void)esp32_mquickjs_event_queue_try_send_from_callback(queue, &event);
                    handled = true;
                }
                if (esp32_mquickjs_wifi_mesh_event_commit(session, event.sequence)) break;
            }
            esp32_mquickjs_wifi_mesh_session_status_t status;
            esp32_mquickjs_wifi_mesh_session_status(session, &status);
            if (status.closing) esp32_mquickjs_event_queue_request_close(queue);
            esp32_mquickjs_wifi_mesh_session_release(session);
        }
        esp32_mquickjs_event_queue_release(queue);
    }
    return handled;
}

JSValue js_wifi_mesh_watch(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{
    esp32_mquickjs_wifi_mesh_session_t *session = mesh_receiver(ctx, *self);
    if (!session) return JS_EXCEPTION;
    if (!esp32_mquickjs_wifi_mesh_session_retain(session)) return JS_ThrowInternalError(ctx, "Mesh reference exhausted");
    JSGCRef options_ref, field_ref, queue_ref;
    JSValue *options = JS_PushGCRef(ctx, &options_ref), *field = JS_PushGCRef(ctx, &field_ref);
    JSValue *queue = JS_PushGCRef(ctx, &queue_ref);
    uint32_t capacity = 8;
    if (argc > 1) goto invalid;
    *options = argc ? argv[0] : JS_UNDEFINED;
    if (!JS_IsUndefined(*options)) {
        static const char *const keys[] = {"capacity"};
        if (!esp32_mquickjs_validate_plain_options(ctx, *options, "WiFiMeshSession.watch", keys, 1)) goto fail;
        *field = JS_GetPropertyStr(ctx, *options, "capacity"); if (JS_IsException(*field)) goto fail;
        if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, MESH_WATCH_CAPACITY, &capacity)) goto invalid;
    }
    esp32_mquickjs_wifi_mesh_session_status_t status;
    esp32_mquickjs_wifi_mesh_session_status(session, &status);
    if (status.closing || status.retired) { mesh_error(ctx, "WiFiMeshSession.watch", session, ESP_ERR_INVALID_STATE, false); goto fail; }
    esp32_mquickjs_runtime_t *runtime = esp32_mquickjs_get_active_runtime();
    if (!runtime) { JS_ThrowInternalError(ctx, "Mesh watch requires an active runtime"); goto fail; }
    mesh_watch_source_t *source = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, sizeof(*source),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!source) { JS_ThrowOutOfMemory(ctx); goto fail; }
    unsigned slot = MESH_WATCH_HANDLES; bool duplicate = false;
    portENTER_CRITICAL(&s_mesh_watch_lock);
    for (unsigned i = 0; i < MESH_WATCH_HANDLES; ++i) {
        if (!s_mesh_watch_sources[i]) slot = i;
        else if (s_mesh_watch_sources[i]->session == session) duplicate = true;
    }
    bool available = !duplicate && slot != MESH_WATCH_HANDLES && s_mesh_watch_handles < MESH_WATCH_HANDLES;
    if (available) ++s_mesh_watch_handles;
    portEXIT_CRITICAL(&s_mesh_watch_lock);
    if (!available) { esp32_mquickjs_memory_payload_free(source); JS_ThrowInternalError(ctx, "Mesh watch already open or queue budget retained"); goto fail; }
    source->session = session; session = NULL;
    *queue = esp32_mquickjs_event_queue_new_wireless("wifi.mesh", ctx, runtime, sizeof(esp32_mquickjs_wifi_mesh_notice_t),
        capacity, ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST, mesh_watch_to_js, NULL, mesh_watch_closed, source);
    if (JS_IsException(*queue)) { mesh_watch_closed(source); goto fail; }
    source->queue = esp32_mquickjs_event_queue_from_value(ctx, *queue);
    source->context_bound = esp32_mquickjs_event_queue_bind_context_release(source->queue, mesh_watch_destroyed);
    if (!source->context_bound) {
        (void)esp32_mquickjs_event_queue_dispose(ctx, *queue);
        JS_ThrowInternalError(ctx, "Mesh queue lifetime binding failed"); goto fail;
    }
    portENTER_CRITICAL(&s_mesh_watch_lock); s_mesh_watch_sources[slot] = source; portEXIT_CRITICAL(&s_mesh_watch_lock);
    JSValue result = JS_PopGCRef(ctx, &queue_ref);
    JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref); return result;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "Mesh watch capacity must be an integer in 1..16");
fail:
    esp32_mquickjs_wifi_mesh_session_release(session);
    JS_PopGCRef(ctx, &queue_ref); JS_PopGCRef(ctx, &field_ref); JS_PopGCRef(ctx, &options_ref); return JS_EXCEPTION;
}
