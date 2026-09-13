
#define JS_NULL 1
typedef struct {
    JSValue (*to_js)(JSContext *,const void *,void *);
    void (*drop)(void *,void *);
    void *opaque;
} esp32_mquickjs_event_queue_t;
typedef struct {
    esp32_mquickjs_event_queue_t *queue;
    bool native_queue_retained,received,event_finished,queue_retained;
    void *event,*timer,*ctx;
    JSGCRef queue_ref;
} esp32_mquickjs_future_driver_state_t;
static int released_queue,released_storage;
static void esp_timer_stop(void *p) { (void)p; }
static void esp_timer_delete(void *p) { (void)p; }
static void JS_DeleteGCRef(void *ctx,JSGCRef *ref) { (void)ctx;(void)ref; }
static void heap_caps_free(void *p) { (void)p;released_storage++; }
static void event_queue_resource_release(void *p,void *queue) { (void)queue;heap_caps_free(p); }
static void esp32_mquickjs_event_queue_release(void *q) { (void)q;released_queue++; }
