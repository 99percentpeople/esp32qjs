
/* Allocator boundary; total admission is exercised by test_event_queue_budget. */
#define esp32_mquickjs_memory_wireless_calloc(owner,n,size,kind,role) heap_caps_calloc(n,size,MALLOC_CAP_8BIT)
#define esp32_mquickjs_memory_payload_free heap_caps_free
#define JS_ThrowRangeError JS_ThrowInternalError
typedef char StaticQueue_t[32];
static const esp32_mquickjs_event_queue_resource_ops_t s_event_queue_resource_ops={0};
bool esp32_mquickjs_event_queue_resources_init(esp32_mquickjs_event_queue_resources_t *r,
    size_t size,uint32_t capacity,bool oldest,const esp32_mquickjs_event_queue_resource_ops_t *ops,void *opaque) {
    (void)ops;(void)opaque;assert(size && capacity && !oldest);
    if(fail_resources)return false;
    r->events=r->send_lock=r->drain_scratch=(void *)1;return true;
}
void esp32_mquickjs_event_queue_resources_deinit(esp32_mquickjs_event_queue_resources_t *r,
    const esp32_mquickjs_event_queue_resource_ops_t *ops,void *opaque) {
    (void)ops;(void)opaque;assert(r->events);memset(r,0,sizeof(*r));++resource_frees;
}
static void event_queue_register(esp32_mquickjs_event_queue_t *q) { assert(q); }
static void event_queue_unregister(esp32_mquickjs_event_queue_t *q) { assert(q); }
static void event_queue_wake_receiver(esp32_mquickjs_event_queue_t *q) { assert(q); }
esp32_mquickjs_event_queue_t *esp32_mquickjs_event_queue_from_value(JSContext *ctx,JSValue value) {
    (void)ctx;assert(value==17);
    esp32_mquickjs_event_queue_t *q=js_opaque;
    if(fail_retain)q->native_retain_count=UINT32_MAX;
    if(fail_bind)atomic_store(&q->closed,true);
    return fail_lookup?NULL:q;
}
static esp32_mquickjs_wifi_monitor_event_t pending;
static bool has_event,queue_full;
bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t *q,const void *event) {
    assert(!critical_depth);
    if(!q || atomic_load(&q->closed) || queue_full || has_event)return false;
    pending=*(const esp32_mquickjs_wifi_monitor_event_t *)event;has_event=true;return true;
}
size_t esp32_mquickjs_event_queue_discard_all(esp32_mquickjs_event_queue_t *q) {
    if(!has_event)return 0;
    has_event=false;q->drop(&pending,q->opaque);return 1;
}
struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_event_queue_t *queue;
    JSContext *ctx;
    JSGCRef queue_ref;
    void *event,*timer;
    bool received,event_finished,queue_retained,native_queue_retained;
};
