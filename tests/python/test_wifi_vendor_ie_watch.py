"""Deferred production broker, capture/close and VM conversion checks.

SDK callbacks/locks and EventQueue send/retain are injected boundaries. This does
not qualify actual FreeRTOS scheduling, SDK unregister, EventQueue wake/reaper,
public constructor OOM, RF reception or the later shared W-09 budget.
Do not import/compile/run until the Wi-Fi stage validation is authorized.
"""
import re
import tempfile
import unittest
from test_wifi_config_controls import sdk_types, structure
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import build, extract, run

DIRECTORY = COMPONENT / 'src/modules/wifi_vendor_ie'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_vendor_ie_watch.h'


def declarations():
    return (PRELUDE + sdk_types('esp32c5/representative', ('wifi_vendor_ie_type_t', 'vendor_ie_data_t'))
            + structure(HEADER.read_text(), 'esp32_mquickjs_wifi_vendor_ie_broker_status_t'))


class WiFiVendorIeWatch(unittest.TestCase):
    def test_broker_registration_uncertainty_drain_suffix_and_stale_generation(self):
        code = declarations() + BROKER_BOUNDARIES
        code += unit(DIRECTORY / 'esp32_mquickjs_wifi_vendor_ie_broker.c')
        compile_run(self, code + BROKER_MAIN)

    def test_capture_copies_filters_full_lock_busy_close_and_retired_charge(self):
        code = capture_code()
        compile_run(self, code + CAPTURE_MAIN)

    def test_converter_nth_allocation_and_moving_gc(self):
        source = (DIRECTORY / 'esp32_mquickjs_wifi_vendor_ie_watch.c').read_text()
        code = capture_code(vm=True)
        code += ''.join(extract(source, name) for name in (
            'vendor_watch_frame_name', 'vendor_watch_to_js', 'esp32_mquickjs_wifi_vendor_ie_watch_status',
            'vendor_watch_hex', 'vendor_watch_oui'))
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            run([str(binary)])


def capture_code(vm=False):
    source = (DIRECTORY / 'esp32_mquickjs_wifi_vendor_ie_watch.c').read_text()
    code = declarations() + CAPTURE_BOUNDARIES
    if not vm:
        code += '#define heap_caps_free free\n'
    code += source[source.index('#define VENDOR_WATCH_MAX_SOURCES'):source.index('static void vendor_watch_count')]
    code += CAPTURE_SEND
    for name in ('vendor_watch_count', 'vendor_watch_matches', 'esp32_mquickjs_wifi_vendor_ie_watch_capture',
                 'vendor_watch_closed', 'vendor_watch_destroyed', 'esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime'):
        code += extract(source, name)
    return code


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES 257
#define WIFI_VENDOR_IE_ELEMENT_ID 221
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_TIMEOUT -3
typedef int esp_err_t;
'''

BROKER_BOUNDARIES = r'''
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical,sdk_calls,captured;
#define portENTER_CRITICAL(p) do {(void)(p);assert(!critical);critical=1;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(critical);critical=0;} while(0)
typedef void (*sdk_callback_t)(void *,wifi_vendor_ie_type_t,const uint8_t[6],const vendor_ie_data_t *,int);
static sdk_callback_t sdk_callback;static void *sdk_context;
static int sdk_error;
static bool synchronous,unregister_inside;
esp_err_t esp32_mquickjs_wifi_vendor_ie_broker_unregister(uint32_t generation);
static int esp_wifi_set_vendor_ie_cb(sdk_callback_t cb,void *ctx) {
    assert(!critical);++sdk_calls;sdk_callback=cb;sdk_context=ctx;
    if(synchronous && cb)cb(ctx,WIFI_VND_IE_TYPE_BEACON,(void *)1,(void *)1,-42);
    return sdk_error;
}
void esp32_mquickjs_wifi_vendor_ie_watch_capture(uint32_t generation,wifi_vendor_ie_type_t frame,
        const uint8_t address[6],const vendor_ie_data_t *data,int rssi) {
    (void)frame;(void)address;(void)data;assert(!critical && generation && rssi==-42);++captured;
    if(unregister_inside)assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(generation)==ESP_ERR_TIMEOUT);
}
'''

BROKER_MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_vendor_ie_broker_status_t status;
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(0)==ESP_ERR_INVALID_ARG && !sdk_calls);
    synchronous=true;assert(esp32_mquickjs_wifi_vendor_ie_broker_register(7)==ESP_OK && !captured);
    sdk_callback_t stale=sdk_callback;void *cookie=sdk_context;
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(7)==ESP_OK && sdk_calls==1);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(8)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_vendor_ie_broker_reset(7));
    sdk_callback(sdk_context,0,NULL,NULL,-42);assert(captured==1);
    sdk_error=77;assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(7)==77);
    esp32_mquickjs_wifi_vendor_ie_broker_status(&status);
    assert(status.uncertain && status.registered && !status.accepting && status.error==77);
    stale(cookie,0,(void *)1,(void *)1,-42);assert(captured==1);
    assert(!esp32_mquickjs_wifi_vendor_ie_broker_reset(7));
    sdk_error=0;assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(7)==ESP_OK && sdk_calls==3);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(7)==ESP_OK && sdk_calls==3);
    assert(!esp32_mquickjs_wifi_vendor_ie_broker_reset(8));
    assert(esp32_mquickjs_wifi_vendor_ie_broker_reset(7));
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(8)==ESP_OK && !s_vendor_broker.callbacks_active);
    stale(cookie,0,(void *)1,(void *)1,-42);assert(captured==1);
    unregister_inside=true;sdk_callback(sdk_context,0,NULL,NULL,-42);
    assert(captured==2 && sdk_calls==5);
    esp32_mquickjs_wifi_vendor_ie_broker_status(&status);
    assert(status.unregister_written && !status.callbacks_active && status.error==ESP_ERR_TIMEOUT);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(8)==ESP_OK && sdk_calls==5);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_reset(8));
    sdk_error=88;assert(esp32_mquickjs_wifi_vendor_ie_broker_register(9)==88);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_register(9)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_vendor_ie_broker_status(&status);
    assert(status.uncertain && !status.registered && !status.accepting);
    sdk_error=0;assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(9)==ESP_OK);
    assert(esp32_mquickjs_wifi_vendor_ie_broker_reset(9));
    assert(!critical);return 0;
}
'''

CAPTURE_BOUNDARIES = r'''
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
'''

CAPTURE_SEND = r'''
static vendor_watch_event_t last;
static bool esp32_mquickjs_event_queue_try_send_from_callback(esp32_mquickjs_event_queue_t *queue,const void *event) {
    assert(locked);if(queue->full)return false;++queue->count;last=*(const vendor_watch_event_t *)event;return true;
}
'''

CAPTURE_MAIN = r'''
int main(void) {
    uint8_t bytes[257]={221,255,1,2,3,4},address[6]={2,3,4,5,6,7};bytes[256]=255;
    esp32_mquickjs_event_queue_t queues[4]={0};vendor_watch_source_t sources[4]={0};
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,(void *)1,(void *)1,-42);
    s_vendor_watch_mutex=&s_vendor_watch_mutex_storage;
    for(unsigned i=0;i<4;++i){sources[i].queue=&queues[i];s_vendor_sources[i]=&sources[i];}
    sources[1].oui_count=1;sources[1].oui[0][0]=99;
    sources[2].oui_count=1;memcpy(sources[2].oui[0],bytes+2,3);queues[3].full=true;
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,address,(void *)bytes,-42);
    assert(queues[0].count==1 && !queues[1].count && queues[2].count==1 && !queues[3].count);
    assert(last.length==257 && last.sequence==1 && last.generation==7 && last.timestamp_us==123);
    assert(last.rssi==-42 && !memcmp(last.bytes,bytes,257) && !memcmp(last.address,address,6));
    memset(bytes,0,sizeof(bytes));assert(last.bytes[256]==255 && last.bytes[0]==221);
    assert(atomic_load(&s_vendor_watch_filtered)==1 && atomic_load(&s_vendor_watch_dropped)==1);
    lock_busy=true;esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,(void *)1,(void *)1,-42);lock_busy=false;
    assert(atomic_load(&s_vendor_watch_busy)==1 && s_vendor_watch_sequence==1);
    for(unsigned i=0;i<3;++i)esp32_mquickjs_wifi_vendor_ie_watch_capture(7,i==0?99:0,i==1?NULL:address,(void *)bytes,-42);
    assert(atomic_load(&s_vendor_watch_invalid)==3 && s_vendor_watch_sequence==1);
    bytes[0]=221;bytes[1]=4;now=-1;
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,address,(void *)bytes,-42);
    assert(atomic_load(&s_vendor_watch_invalid)==4);now=123;
    s_vendor_watch_sequence=VENDOR_WATCH_MAX_SEQUENCE;
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,address,(void *)bytes,-42);assert(queues[0].count==1);
    vendor_watch_closed(&sources[0]);assert(!s_vendor_sources[0]);
    queues[1].disposed=true;s_vendor_watch_handles=3;s_vendor_watch_capacity=24;
    esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime();
    for(unsigned i=0;i<4;++i)assert(!s_vendor_sources[i]);
    assert(!queues[1].closes && queues[2].closes==1 && queues[3].closes==1);
    assert(queues[2].retains==1 && queues[2].releases==1);
    assert(s_vendor_watch_handles==3 && s_vendor_watch_capacity==24);
    for(unsigned i=0;i<3;++i){vendor_watch_source_t *p=calloc(1,sizeof(*p));p->capacity=8;vendor_watch_destroyed(p);}
    assert(!s_vendor_watch_handles && !s_vendor_watch_capacity && !locked);return 0;
}
'''

VM_MAIN = r'''
int main(void) {
    vendor_watch_event_t event={.sequence=9007199254740991ULL,.timestamp_us=123,.generation=7,
        .rssi=-42,.length=257,.frame=4,.address={2,3,4,5,6,7},.bytes={221,255,1,2,3,4}};
    event.bytes[256]=255;
    for(int scenario=0;scenario<2;++scenario){
        int total=1;
        for(int nth=0;nth<=total;++nth){
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef ref,data_ref;JSValue *result=JS_PushGCRef(ctx,&ref);
            calls=0;fail_at=nth;collect=inject=true;
            *result=scenario?esp32_mquickjs_wifi_vendor_ie_watch_status(ctx):vendor_watch_to_js(ctx,&event,NULL);
            if(nth==0){assert(!JS_IsException(*result));total=calls;}else assert(JS_IsException(*result));
            collect=inject=false;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else if(!scenario){
                JSValue *data=JS_PushGCRef(ctx,&data_ref);*data=JS_GetPropertyStr(ctx,*result,"data");
                assert(JS_IsArray(ctx,*data));uint32_t value;
                assert(!JS_ToUint32(ctx,&value,JS_GetPropertyStr(ctx,*data,"length")) && value==257);
                assert(!JS_ToUint32(ctx,&value,JS_GetPropertyUint32(ctx,*data,256)) && value==255);
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"interface")));
                double sequence;assert(!JS_ToNumber(ctx,&sequence,JS_GetPropertyStr(ctx,*result,"sequence")) && sequence==9007199254740991.0);
                JS_PopGCRef(ctx,&data_ref);
            }
            uint8_t oui[3];assert(vendor_watch_oui(ctx,JS_NewString(ctx,"Aa:BB:cC"),oui) && oui[0]==170 && oui[2]==204);
            assert(!vendor_watch_oui(ctx,JS_NewString(ctx,"AA:BB:CC00"),oui));
            assert(!vendor_watch_oui(ctx,JS_NewString(ctx,"AA:BG:CC"),oui));
            JS_PopGCRef(ctx,&ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
'''
