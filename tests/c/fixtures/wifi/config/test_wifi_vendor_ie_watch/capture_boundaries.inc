
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
typedef int StaticSemaphore_t;
typedef void *SemaphoreHandle_t;
typedef struct {unsigned count;bool full,disposed;unsigned retains,closes,releases;} esp32_mquickjs_event_queue_t;
static bool lock_busy;static unsigned locked;
static int64_t now=123;
static int xSemaphoreTake(void *mutex,unsigned ticks) {
    assert(mutex && !locked);if(!ticks && lock_busy)return 0;locked=1;return 1;
}
static void xSemaphoreGive(void *mutex){assert(mutex && locked);locked=0;}
static int64_t esp_timer_get_time(void){assert(locked);return now;}
static bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t *queue) {
    assert(locked);if(queue->disposed)return false;++queue->retains;return true;
}
static bool esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *queue) {
    assert(!locked);++queue->closes;return true;
}
static void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t *queue) {
    assert(!locked);++queue->releases;
}
static void esp32_mquickjs_wifi_vendor_ie_broker_status(esp32_mquickjs_wifi_vendor_ie_broker_status_t *out) {
    memset(out,0,sizeof(*out));out->generation=7;out->registered=true;
}
