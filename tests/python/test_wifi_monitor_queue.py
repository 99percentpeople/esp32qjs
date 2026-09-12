"""Deferred production Monitor bridge and EventQueue context ownership tests.

JS/SDK resource creation, queue transport and wake scheduling are boundaries.
Constructor, context binding, native lifetime and Future finish/destroy are real.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_wifi_monitor_resources import production_code
import test_wifi_rx_target as rx_target
from test_wireless_control_regression import function


def production_queue_code(profile):
    source = (rx_target.ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs_event_queue.c').read_text()
    start = source.index('struct esp32_mquickjs_event_queue {')
    queue_struct = source[start:source.index('\n};', start) + 4]
    code = production_code(profile) + JS_BOUNDARY
    code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_event_queue.h')
    code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_event_queue_resources.h')
    code += queue_struct + QUEUE_BOUNDARY
    for name in ['event_queue_resource_release', 'event_queue_new', 'esp32_mquickjs_event_queue_new_wireless', 'event_queue_destroy_native', 'event_queue_take_destroy_ownership_locked',
                 'esp32_mquickjs_event_queue_bind_context_release', 'esp32_mquickjs_event_queue_retain',
                 'esp32_mquickjs_event_queue_release', 'esp32_mquickjs_event_queue_close',
                 'esp32_mquickjs_event_queue_dispose', 'esp32_mquickjs_event_queue_new',
                 'event_queue_future_finish', 'event_queue_future_destroy']:
        code += function(source, name)
    code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_monitor_queue.h')
    return code + rx_target.unit(rx_target.ROOT / 'components/esp32_mquickjs/src/modules/wifi_monitor' /
                                 'esp32_mquickjs_wifi_monitor_queue.c')


class WiFiMonitorQueue(unittest.TestCase):
    def test_context_outlives_dispose_and_conversion_failure_does_not_leak_root(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = production_queue_code(profile)
                path, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                path.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror',
                                        str(path), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


JS_BOUNDARY = r'''
typedef int JSContext;
typedef int esp32_mquickjs_runtime_t;
typedef intptr_t JSValue;
typedef struct { JSValue value; } JSGCRef;
typedef struct { uint32_t generation; } esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
#define JS_EXCEPTION ((JSValue)-1)
#define JS_UNDEFINED ((JSValue)0)
#define JS_NULL ((JSValue)1)
#define JS_CLASS_EVENT_QUEUE 17
#define JS_IsException(value) ((value)==JS_EXCEPTION)
#define JS_IsUndefined(value) ((value)==JS_UNDEFINED)
#define MALLOC_CAP_8BIT 0
#define portMAX_DELAY 0
#define pdTRUE 1
typedef void *SemaphoreHandle_t;
static unsigned roots,queue_allocations,queue_frees,resource_frees,closed_calls,context_refs;
static unsigned sdk_allocation_calls,fail_sdk_allocation_at;
static bool fail_allocation,fail_resources,fail_object,fail_lookup,fail_retain,fail_bind,fail_context,fail_frame;
static void *js_opaque;
static JSValue *JS_PushGCRef(JSContext *ctx,JSGCRef *ref) { (void)ctx;++roots;ref->value=JS_UNDEFINED;return &ref->value; }
static JSValue JS_PopGCRef(JSContext *ctx,JSGCRef *ref) { (void)ctx;assert(roots);--roots;return ref->value; }
static void JS_DeleteGCRef(JSContext *ctx,JSGCRef *ref) { (void)ctx;(void)ref;assert(roots);--roots; }
static JSValue JS_ThrowInternalError(JSContext *ctx,const char *text) { (void)ctx;assert(text);return JS_EXCEPTION; }
static JSValue JS_ThrowReferenceError(JSContext *ctx,const char *text) { return JS_ThrowInternalError(ctx,text); }
static JSValue JS_ThrowOutOfMemory(JSContext *ctx) { (void)ctx;return JS_EXCEPTION; }
static JSValue JS_NewObjectClassUser(JSContext *ctx,int id) { (void)ctx;assert(roots && id==JS_CLASS_EVENT_QUEUE);return fail_object?JS_EXCEPTION:17; }
static void JS_SetOpaque(JSContext *ctx,JSValue value,void *opaque) { (void)ctx;assert(value==17);js_opaque=opaque; }
static void *JS_GetOpaque(JSContext *ctx,JSValue value) { (void)ctx;assert(value==17);return js_opaque; }
static int JS_GetClassID(JSContext *ctx,JSValue value) { (void)ctx;return (int)value; }
static void *heap_caps_calloc(size_t count,size_t size,int caps) {
    (void)caps;assert(!critical_depth);++sdk_allocation_calls;
    if(fail_allocation || (fail_sdk_allocation_at && sdk_allocation_calls==fail_sdk_allocation_at))return NULL;
    void *p=calloc(count,size);assert(p);++queue_allocations;return p;
}
static void heap_caps_free(void *p) { assert(!critical_depth);if(p){++queue_frees;free(p);} }
static int xSemaphoreTake(SemaphoreHandle_t lock,int timeout) { (void)timeout;assert(lock && !critical_depth);return pdTRUE; }
static void xSemaphoreGive(SemaphoreHandle_t lock) { assert(lock && !critical_depth); }
static void esp_timer_stop(void *timer) { assert(timer); }
static void esp_timer_delete(void *timer) { assert(timer); }
'''

QUEUE_BOUNDARY = r'''
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
'''

MAIN = r'''
static JSContext ctx;
static esp32_mquickjs_runtime_t runtime;
static esp32_mquickjs_wifi_monitor_resources_t resources;
static esp32_mquickjs_wifi_monitor_queue_t bridge;
static unsigned pool_allocations;
static void *pool_calloc(size_t count,size_t size,void *opaque) { (void)opaque;++pool_allocations;return calloc(count,size); }
static void *pool_malloc(size_t size,void *opaque) { return pool_calloc(1,size,opaque); }
static void pool_free(void *p,void *opaque) { (void)opaque;assert(pool_allocations);--pool_allocations;free(p); }
static bool retain_context(void *opaque) { assert(opaque==&bridge);if(fail_context)return false;++context_refs;return true; }
static void release_context(void *opaque) { assert(opaque==&bridge && context_refs && !critical_depth);--context_refs; }
static void request_close(void *opaque) { assert(opaque==&bridge);++closed_calls; }
static JSValue make_frame(JSContext *context,const esp32_mquickjs_wifi_monitor_event_t *event,void *opaque) {
    (void)context;assert(opaque==&bridge);
    esp32_mquickjs_wifi_monitor_info_t info;
    assert(esp32_mquickjs_wifi_monitor_frame_info(&resources,event,&info));
    if(fail_frame)return JS_EXCEPTION;
    assert(retain_context(opaque)); /* Frame holds context independently of queue. */
    return 99;
}
static void setup(void) {
    assert(!pool_allocations && !context_refs && !roots && !has_event);
    memset(&resources,0,sizeof(resources));memset(&bridge,0,sizeof(bridge));
    bridge.resources=&resources;bridge.make_frame=make_frame;bridge.retain_context=retain_context;
    bridge.release_context=release_context;bridge.request_close=request_close;bridge.opaque=&bridge;
    esp32_mquickjs_wifi_monitor_allocator_t allocator={pool_calloc,pool_malloc,pool_free,NULL};
    assert(esp32_mquickjs_wifi_monitor_resources_init(&resources,1,1,24,false,&allocator,
           esp32_mquickjs_wifi_monitor_queue_publish,&bridge));
}
static void cleanup(void) {
    assert(!context_refs && !roots && !has_event && !bridge.queue && !atomic_load(&bridge.context_owned));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,false));
    assert(esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(!pthread_mutex_destroy(&resources.lock));
}
static void constructor_failures(void) {
    bool *flags[]={&fail_context,&fail_allocation,&fail_resources,&fail_object,&fail_lookup,&fail_bind};
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        setup();*flags[i]=true;
        assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==JS_EXCEPTION);
        *flags[i]=false;cleanup();
    }
    setup();
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,129)==JS_EXCEPTION);
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==JS_EXCEPTION);
    cleanup();
}
static void late_future(const esp32_mquickjs_wifi_rx_target_view_t *view,bool finish,bool failure) {
    setup();fail_frame=failure;
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==17 && context_refs==1);
    esp32_mquickjs_event_queue_t *queue=bridge.queue;
    assert(!esp32_mquickjs_event_queue_bind_context_release(queue,monitor_queue_release_context));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    queue_full=true;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,1)==ESP32_MQUICKJS_WIFI_MONITOR_QUEUE_FULL);
    queue_full=false;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,2)==ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED);
    esp32_mquickjs_future_driver_state_t *future=calloc(1,sizeof(*future));assert(future);
    future->event=malloc(sizeof(pending));assert(future->event);memcpy(future->event,&pending,sizeof(pending));has_event=false;
    esp32_mquickjs_wifi_monitor_event_t delivered=pending;
    future->queue=queue;future->received=true;future->native_queue_retained=true;
    assert(esp32_mquickjs_event_queue_retain(queue));
    esp32_mquickjs_wifi_monitor_queue_detach(&bridge);
    assert(!bridge.queue && atomic_load(&bridge.context_owned) && context_refs==1);
    unsigned before_free=queue_frees;
    assert(esp32_mquickjs_event_queue_dispose(&ctx,17) && !js_opaque && context_refs==1 && queue_frees==before_free);
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==JS_EXCEPTION);
    assert(!esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    if(finish)assert(event_queue_future_finish(&ctx,future)==(failure?JS_EXCEPTION:99));
    event_queue_future_destroy(future);
    assert(context_refs==(finish && !failure?1U:0U) && !atomic_load(&bridge.context_owned));
    if(finish && !failure) {
        assert(esp32_mquickjs_wifi_monitor_close_frame(&resources,&delivered));
        release_context(&bridge);
    }
    assert(!esp32_mquickjs_wifi_monitor_discard_event(&resources,&delivered));
    cleanup();fail_frame=false;
}
static void runtime_close(const esp32_mquickjs_wifi_rx_target_view_t *view) {
    setup();assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==17);
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,3)==ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED);
    unsigned before=closed_calls;
    assert(esp32_mquickjs_event_queue_close(bridge.queue) && closed_calls==before+1);
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,4)==ESP32_MQUICKJS_WIFI_MONITOR_CLOSING);
    assert(!esp32_mquickjs_event_queue_close(bridge.queue) && closed_calls==before+1);
    assert(esp32_mquickjs_event_queue_dispose(&ctx,17) && context_refs==1 && !has_event);
    esp32_mquickjs_wifi_monitor_queue_detach(&bridge);
    esp32_mquickjs_wifi_monitor_queue_detach(&bridge);
    cleanup();
}
int main(void) {
    wifi_pkt_rx_ctrl_t rx={0};rx.sig_len=24;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=24;
#endif
    uint8_t packet[sizeof(rx)+24];memcpy(packet,&rx,sizeof(rx));memset(packet+sizeof(rx),0,24);packet[sizeof(rx)]=8;
    esp32_mquickjs_wifi_rx_target_view_t view;
    assert(esp32_mquickjs_wifi_rx_target_view(packet,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    constructor_failures();late_future(&view,false,false);late_future(&view,true,true);late_future(&view,true,false);runtime_close(&view);
    assert(!critical_depth && !roots && !pool_allocations && !context_refs && queue_allocations && resource_frees);
}
'''
