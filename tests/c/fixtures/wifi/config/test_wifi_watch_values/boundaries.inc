
#include <stdatomic.h>
#include <math.h>
#include "cutils.h"
#define ESP32_MQUICKJS_WIFI_MAX_WATCHERS 4
#define WIFI_WATCH_MAX_SEQUENCE UINT64_C(9007199254740991)
#define pdTRUE 1
static void *s_watch_mutex=(void *)1,*s_ingress=(void *)2;
static uint64_t s_sequence;
static uint32_t s_ingress_high_water;
static _Atomic uint32_t s_ingress_dropped;
static wifi_watch_event_t queued_event;
static int locked,sends,wakes;static bool lock_fail,queue_fail;
typedef void esp32_mquickjs_runtime_t;
typedef void esp32_mquickjs_event_queue_t;
static struct {void *runtime,*queue;uint64_t after_sequence,mask;bool all,raw;} source={.runtime=(void *)3,.queue=(void *)4,.all=true};
typedef __typeof__(source) wifi_watch_source_t;
static wifi_watch_source_t *s_sources[4]={&source};
#define portMAX_DELAY UINT32_MAX
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
static int critical;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
static bool queued_ready,subscriber_fail;
static unsigned uxQueueMessagesWaiting(void *queue){assert(queue==s_ingress && locked);return queued_ready?1:0;}
static wifi_watch_event_t subscriber_events[4];
static int subscriber_sends;
static int xQueuePeek(void *q,void *e,unsigned t){assert(q==s_ingress && !t && locked);if(!queued_ready)return 0;*(wifi_watch_event_t *)e=queued_event;return 1;}
static int xQueueReceive(void *q,void *e,unsigned t){int ok=xQueuePeek(q,e,t);if(ok)queued_ready=false;return ok;}
#define esp32_mquickjs_memory_queue_delete vQueueDelete
static void vQueueDelete(void *q){assert(q==s_ingress && locked && !queued_ready);}
static bool esp32_mquickjs_wifi_watch_control_ready(int id,uint32_t generation){(void)id;(void)generation;return true;}
static bool esp32_mquickjs_event_queue_send(void *q,const void *e){assert(q==source.queue && locked);subscriber_sends++;if(subscriber_fail)return false;assert(subscriber_sends<=4);subscriber_events[subscriber_sends-1]=*(const wifi_watch_event_t *)e;return true;}

static int xSemaphoreTake(void *mutex,unsigned ticks) {assert(mutex==s_watch_mutex && (ticks==0 || ticks==portMAX_DELAY) && !locked);if(lock_fail)return 0;locked=1;return 1;}
static void xSemaphoreGive(void *mutex) {assert(mutex==s_watch_mutex && locked);locked=0;}
static uint64_t esp_timer_get_time(void) {return 456;}
static int xQueueSend(void *queue,const void *event,unsigned ticks) {
    assert(queue==s_ingress && !ticks && locked);sends++;
    if(queue_fail)return 0;queued_event=*(const wifi_watch_event_t *)event;queued_ready=true;return 1;
}
static void esp32_mquickjs_notify_activity(void *runtime) {assert(runtime==source.runtime && locked);wakes++;}
