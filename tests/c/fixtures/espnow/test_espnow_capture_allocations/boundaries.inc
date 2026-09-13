
static espnow_session_t s_espnow_session;static atomic_uint s_espnow_next_generation=1;
static unsigned queue_capacity,radio_live,queue_retains;static int queue_value;static bool queue_closed,parse_busy;
static bool boundary_failure(void) {return inject && ++calls==fail_at;}
static void *esp32_mquickjs_memory_payload_alloc(const char *name,size_t n,int policy) { (void)name;return heap_caps_malloc(n,policy); }
static void *esp32_mquickjs_memory_payload_calloc(const char *name,size_t n,size_t size,int policy) { (void)name;return heap_caps_calloc(n,size,policy); }
static void *esp32_mquickjs_get_active_runtime(void) {return (void *)1;}
/* Parsing has its own validation tests; this fixture fixes bounded options so
 * every resource creation/registration boundary in production capture runs. */
static bool espnow_parse_open_options(JSContext *ctx,int argc,JSGCRef *argv,esp32_mquickjs_future_driver_state_t *s) {
    (void)ctx;(void)argc;(void)argv;s->receive_capacity=2;s->max_payload_bytes=64;s->tx_queue_capacity=queue_capacity;s->has_pmk=true;memset(s->pmk,0x5a,16);if(parse_busy)atomic_store(&s_espnow_session.cleanup_scheduled,true);return true;
}
static JSValue espnow_receive_event_to_js(JSContext *ctx,const void *e,void *p) { (void)ctx;(void)e;(void)p;return JS_UNDEFINED; }
static void espnow_receive_event_drop(void *e,void *p) { (void)e;(void)p; }
static void espnow_event_queue_close(void *p) { (void)p; }
static JSValue esp32_mquickjs_event_queue_new(JSContext *ctx,void *r,size_t size,unsigned cap,int policy,void *convert,void *drop,void *close,void *opaque) {
    (void)r;(void)size;(void)cap;(void)policy;(void)convert;(void)drop;(void)close;(void)opaque;return JS_NewObject(ctx);
}
static void *esp32_mquickjs_event_queue_from_value(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return &queue_value; }
static bool esp32_mquickjs_event_queue_retain(void *q) {assert(q);if(boundary_failure())return false;queue_retains++;return true;}
static void esp32_mquickjs_event_queue_release(void *q) {assert(q && queue_retains);queue_retains--;}
static bool esp32_mquickjs_event_queue_dispose(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return true; }
static bool esp32_mquickjs_event_queue_discard_all(void *p) { (void)p;return true; }
static bool esp32_mquickjs_event_queue_is_closed(void *p) { (void)p;return queue_closed; }
static bool esp32_mquickjs_event_queue_close(void *p) { (void)p;queue_closed=true;return true; }
static int esp32_mquickjs_wifi_radio_acquire(int client,int mode,esp32_mquickjs_wifi_radio_lease_t *lease) { (void)client;(void)mode;if(boundary_failure())return 1;lease->acquired=true;radio_live++;return 0; }
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *lease) { if(lease->acquired){assert(radio_live);radio_live--;lease->acquired=false;} }
static int64_t esp_timer_get_time(void) {return 1;}
static bool esp32_mquickjs_future_wake(void *r,int t) { (void)r;(void)t;return true; }
static void espnow_notify_tx_worker(espnow_session_t *s) { (void)s; }
static void espnow_request_reap(espnow_session_t *s) { (void)s;assert(0); }
static bool espnow_schedule_background_close(espnow_session_t *s) { (void)s;assert(!"capture cleanup should be synchronous before driver start");return false; }
static bool esp32_mquickjs_wireless_close_can_release(unsigned n,bool quiescent) {return n==0 && quiescent;}
static void vTaskDelete(void *p) { (void)p;assert(0); }
static int esp_now_unregister_recv_cb(void) {assert(0);return 0;}
static int esp_now_unregister_send_cb(void) {assert(0);return 0;}
static int esp_now_set_wake_window(int n) { (void)n;assert(0);return 0;}
static int esp32_mquickjs_wifi_radio_interval_release(esp32_mquickjs_wifi_radio_lease_t *lease,esp32_mquickjs_wifi_interval_token_t *token) {(void)lease;assert(!token->identity);return ESP_OK;}
static int esp_now_deinit(void) {assert(0);return 0;}
static void espnow_note_native_deinit(void) {assert(0);}
