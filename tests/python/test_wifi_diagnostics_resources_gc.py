"""Production Monitor/CSI result converters in the real VM; execution deferred.

Native ledger boundaries are injected here. Registry ownership and allocator
retirement are covered separately by the production Session/resource fixtures.
This fixture does not prove target struct sizes or concurrent SDK scheduling.
"""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, build, extract, run


class WiFiDiagnosticsResourcesGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        base = ROOT / 'components/esp32_mquickjs/src/modules'
        monitor = (base / 'wifi_monitor/esp32_mquickjs_wifi_monitor.c').read_text()
        csi = (base / 'wifi_csi/esp32_mquickjs_wifi_csi.c').read_text()
        macros = monitor[monitor.index('#define SET('):monitor.index('\n\n', monitor.index('#define SET('))]
        code = BOUNDARIES + macros + '\n'
        code += extract(monitor, 'esp32_mquickjs_wifi_monitor_diagnostics')
        code += extract(csi, 'wifi_csi_lifecycle_name')
        code += extract(csi, 'wifi_csi_store_state_name')
        code += extract(csi, 'esp32_mquickjs_wifi_csi_diagnostics')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_native_values_are_copied_before_gc_and_each_allocation_can_fail(self):
        for state in range(5):
            for collect in (0, 1):
                run([str(self.binary), str(state), str(collect)])


BOUNDARIES = r'''
#include <stdatomic.h>
typedef struct {uint64_t callbacks,accepted,dropped_pool_full,dropped_queue_full,dropped_closing;
    uint32_t leased_frames,publishers;} monitor_counts_t;
typedef struct {bool accepting,identity_exhausted;size_t allocated_bytes;uint32_t free_slots;
    monitor_counts_t counters;} monitor_resources_t;
typedef struct {uint32_t generation;bool closed,close_requested,retirement_blocked;
    monitor_resources_t resources;} esp32_mquickjs_wifi_monitor_diagnostic_t;
typedef struct {char control[29];} esp32_mquickjs_wifi_monitor_session_t;
#define ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS 8U
static int state,locked,observations;
static size_t esp32_mquickjs_wifi_monitor_diagnostics_snapshot(
    esp32_mquickjs_wifi_monitor_diagnostic_t output[8],uint32_t *unavailable){
    assert(!locked && !observations++);*unavailable=1;
    for(unsigned i=0;i<8;i++)output[i]=(esp32_mquickjs_wifi_monitor_diagnostic_t){
        .generation=100+i,.closed=i>0,.close_requested=i>0,.retirement_blocked=i==7,
        .resources={.accepting=i==0,.allocated_bytes=512+i,.free_slots=2,
            .counters={.callbacks=(UINT64_C(1)<<40)+i,.accepted=10+i,.leased_frames=1}}};
    return 8;
}
typedef enum {WIFI_CSI_CLOSED,WIFI_CSI_OPENING,WIFI_CSI_RUNNING,WIFI_CSI_STOPPING,
    WIFI_CSI_STOPPED,WIFI_CSI_FAULTED} wifi_csi_lifecycle_t;
#include "esp32_mquickjs_wifi_csi_store.h"
static struct {uint32_t generation;wifi_csi_lifecycle_t lifecycle;
    bool cleanup_scheduled,close_requested;} s_wifi_csi;
static int s_wifi_csi_store_lock;
static size_t esp32_mquickjs_wifi_csi_rx_native_control_bytes(void){return 128;}
static esp32_mquickjs_wifi_csi_store_t s_wifi_csi_store={.maximum_slots=64,.next_generation=9};
size_t esp32_mquickjs_wifi_csi_store_snapshot(
    esp32_mquickjs_wifi_csi_store_t *store,esp32_mquickjs_wifi_csi_store_snapshot_t output[8]){
    assert(store==&s_wifi_csi_store && !locked && !observations++);
    if(state==3)return 0;
    for(unsigned i=0;i<8;i++)output[i]=(esp32_mquickjs_wifi_csi_store_snapshot_t){
        .generation=71+i,.capacity=4,.bytes=512+i,.free_slots=3,.leased_frames=1,.callbacks=8+i,.accepted=6+i,
        .state=i?ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED:
            state==0?ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE:state==1?ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED:
            state==2?ESP32_MQUICKJS_WIFI_CSI_STORE_RETIRING:ESP32_MQUICKJS_WIFI_CSI_STORE_ALLOCATING};
    return 8;
}

'''

MAIN = r'''
static uint64_t number(JSContext *ctx,JSValue object,const char *name){
    double value;assert(!JS_ToNumber(ctx,&value,JS_GetPropertyStr(ctx,object,name)));return (uint64_t)value;
}
static void string_is(JSContext *ctx,JSValue object,const char *name,const char *expected){
    JSCStringBuf buf;const char *value=JS_ToCString(ctx,JS_GetPropertyStr(ctx,object,name),&buf);
    assert(value && !strcmp(value,expected));
}
int main(int argc,char **argv){
    assert(argc==3);state=atoi(argv[1]);bool moving=atoi(argv[2]);
    for(int kind=0;kind<2;kind++){
        int total=0;
        for(int nth=0;nth<=total;nth++){
            void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
            memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.generation=71;
            s_wifi_csi.lifecycle=state==0?WIFI_CSI_RUNNING:WIFI_CSI_CLOSED;
            s_wifi_csi.close_requested=state!=0;observations=locked=0;
            JSGCRef result_ref,array_ref,item_ref;
            JSValue *result=JS_PushGCRef(ctx,&result_ref),*array=JS_PushGCRef(ctx,&array_ref),*item=JS_PushGCRef(ctx,&item_ref);
            inject=true;collect=moving;fail_at=nth;calls=0;
            *result=kind?esp32_mquickjs_wifi_csi_diagnostics(ctx):esp32_mquickjs_wifi_monitor_diagnostics(ctx);
            inject=false;collect=false;assert(!locked);
            if(!nth)total=calls;
            if(nth){assert(JS_IsException(*result)&&JS_HasException(ctx));JS_GetException(ctx);}
            else {
                assert(!JS_IsException(*result));
                if(kind){
                    assert(observations==1 && number(ctx,*result,"generation")==71);
                    string_is(ctx,*result,"state",state==0?"running":"closed");
                    assert(number(ctx,*result,"reservedSlots")== (state==3?0:32));
                    assert(number(ctx,*result,"storageBytes")== (state==3?0:4124));
                    assert(number(ctx,*result,"activeStorageBytes")== (state==0?512:0));
                    assert(number(ctx,*result,"retainedStorageBytes")== (state==3?0:state==1?4124:3612));
                    assert(number(ctx,*result,"pendingStorageBytes")== (state==2||state==4?512:0));
                    assert(number(ctx,*result,"controlBytes")==sizeof(s_wifi_csi)+sizeof(s_wifi_csi_store)+sizeof(s_wifi_csi_store_lock)+128);
                    *array=JS_GetPropertyStr(ctx,*result,"generations");assert(number(ctx,*array,"length")== (state==3?0:8));
                    for(unsigned i=0;state!=3 && i<8;i++){
                        *item=JS_GetPropertyUint32(ctx,*array,i);
                        assert(number(ctx,*item,"generation")==71+i && number(ctx,*item,"storageBytes")==512+i);
                        bool unavailable=i==0 && (state==2||state==4);
                        if(unavailable){assert(JS_IsNull(JS_GetPropertyStr(ctx,*item,"freeSlots")));assert(JS_IsNull(JS_GetPropertyStr(ctx,*item,"callbacks")));}
                        else {assert(number(ctx,*item,"freeSlots")==3 && number(ctx,*item,"callbacks")==8+i);}
                    }
                } else {
                    assert(observations==1 && number(ctx,*result,"unavailableSessions")==1);
                    *array=JS_GetPropertyStr(ctx,*result,"sessions");assert(number(ctx,*array,"length")==8);
                    for(unsigned i=0;i<8;i++){
                        *item=JS_GetPropertyUint32(ctx,*array,i);
                        assert(number(ctx,*item,"generation")==100+i && number(ctx,*item,"poolBytes")==512+i);
                        assert(number(ctx,*item,"callbacks")== (UINT64_C(1)<<40)+i);
                        assert(JS_GetPropertyStr(ctx,*item,"closed")==JS_NewBool(i>0));
                    }
                }
            }
            JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&array_ref);JS_PopGCRef(ctx,&result_ref);
            assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
        }
    }
}
'''
