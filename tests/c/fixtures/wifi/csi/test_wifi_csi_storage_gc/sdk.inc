
#include "esp32_mquickjs_wifi_csi_resources.h"
#include "esp32_mquickjs_wifi_csi_store.h"
#include "esp32_mquickjs_wifi_csi_wire.h"
#define WIFI_CSI_CLOSED 0
#define WIFI_CSI_RUNNING 1
#define CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_BATCH_FRAMES 2
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
typedef struct { int lock,lifecycle;struct {uint32_t queue_capacity;} options;_Atomic(esp32_mquickjs_wifi_csi_resources_t *) resources;void *event_queue; } wifi_csi_session_t;
static wifi_csi_session_t s_wifi_csi;
static void store_lock(void *p){(void)p;}
static esp32_mquickjs_wifi_csi_store_t s_wifi_csi_store={.lock=store_lock,.unlock=store_lock,.maximum_slots=4,.max_frame_bytes=64,.next_generation=1};
static esp32_mquickjs_wifi_csi_store_result_t allocation_result;
static void wifi_csi_snapshot_stats(wifi_csi_session_t *s) { (void)s; }
static void *pool_calloc(size_t n,size_t size,void *p) { (void)p;return heap_caps_calloc(n,size,1); }
static void *pool_malloc(size_t n,void *p) { (void)p;return heap_caps_malloc(n,1); }
static void pool_free(void *p,void *opaque) { (void)opaque;heap_caps_free(p); }
static esp32_mquickjs_wifi_csi_event_t events[2];static unsigned queued;
static bool publish(const esp32_mquickjs_wifi_csi_event_t *event,void *p) { (void)p;assert(queued<2);events[queued++]=*event;return true; }
static bool esp32_mquickjs_event_queue_try_receive(void *queue,esp32_mquickjs_wifi_csi_event_t *event) {
    (void)queue;if(!queued)return false;*event=events[0];memmove(events,events+1,--queued*sizeof(*events));return true;
}
static int64_t esp_timer_get_time(void) { return 1000; }
static wifi_csi_session_t *wifi_csi_session_from_value(JSContext *ctx,JSValue value,bool closed) { (void)ctx;(void)value;(void)closed;return &s_wifi_csi; }
