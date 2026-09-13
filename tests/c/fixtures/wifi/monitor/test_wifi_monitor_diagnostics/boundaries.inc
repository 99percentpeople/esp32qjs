
typedef struct { int resources; } snapshot_owner_t;
typedef struct { snapshot_owner_t *native; } monitor_session_t;
typedef struct { int unused; } esp32_mquickjs_event_queue_t;
static snapshot_owner_t owner;
static monitor_session_t session={&owner};
static esp32_mquickjs_wifi_monitor_snapshot_t pool_snapshot;
static esp32_mquickjs_event_queue_stats_t queue_snapshot;
static bool queue_available=true;
static unsigned pool_reads,queue_reads;
static const char *esp_get_idf_version(void) { return "fixture-sdk"; }
static monitor_session_t *monitor_session_from_this(JSContext *ctx,JSValue *value) {
    (void)ctx;assert(value);return &session;
}
static void esp32_mquickjs_wifi_monitor_resources_snapshot(int *resources,esp32_mquickjs_wifi_monitor_snapshot_t *out) {
    assert(resources==&owner.resources);++pool_reads;*out=pool_snapshot;
}
static bool esp32_mquickjs_event_queue_get_stats(esp32_mquickjs_event_queue_t *queue,esp32_mquickjs_event_queue_stats_t *out) {
    assert(queue);++queue_reads;if(!queue_available)return false;*out=queue_snapshot;return true;
}
