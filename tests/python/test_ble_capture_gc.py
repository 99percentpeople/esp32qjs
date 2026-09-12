"""Production capture/rollback at SDK boundaries, real argument roots and GC."""
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from wireless_vm_fixture import ROOT,CORE,build,extract,run
SDK=r'''
#include "mquickjs_priv.h"
#include "esp32_mquickjs_native_pool.h"
#define CONFIG_ESP32_MQUICKJS_BLE_SCAN_QUEUE_LEN 2
#define CONFIG_ESP32_MQUICKJS_BLE_SCAN_QUEUE_MAX_LEN 4
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES 64
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES 2
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_CHARACTERISTICS 2
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_DESCRIPTORS 2
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 247
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS 1
#define BLE_HS_ADV_MAX_SZ 31
#define MALLOC_CAP_INTERNAL 2
#define ESP32_MQUICKJS_MEMORY_PINNED_INTERNAL 2
#define ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST 0
#define BLE_HS_EBUSY 1
#define BLE_HS_EINVAL 2
#define BLE_HS_EMSGSIZE 3
#define BLE_DEFAULT_TIMEOUT_MS 5000
#define BLE_HS_FOREVER INT32_MAX
#define BLE_STOP_RUNNING 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
typedef enum { BLE_OP_SCAN,BLE_OP_SCANNER_CLOSE,BLE_OP_GATT_READ,BLE_OP_GATT_WRITE,BLE_OP_EXCHANGE_MTU,BLE_OP_READ_RSSI,BLE_OP_CONNECTION_CLOSE,BLE_OP_DISCOVER } ble_operation_t;
typedef int esp32_mquickjs_event_queue_t;
typedef struct { int unused; } ble_scan_event_t;
typedef struct {
    JSContext *ctx;ble_operation_t operation;unsigned max_services,max_characteristics,max_descriptors,adapter_generation,connection_generation,connection_index,timeout_ms,duration_ms,attribute_handle,max_bytes,mtu,payload_length,native_identity,callback_refs;
    bool include_descriptors,response,owner_rooted,queue_rooted,gatt_accounted,gatt_started,detached;
    JSGCRef owner_ref,queue_ref;atomic_bool completed;uint8_t *payload;
    struct { unsigned itvl,window,passive,filter_duplicates,limited; } scan_params;
} esp32_mquickjs_future_driver_state_t;
typedef struct { int *services,*characteristics,*descriptors;unsigned service_count,characteristic_count,descriptor_count,discovery_generation;unsigned generation;atomic_uint gatt_pending;_Atomic(esp32_mquickjs_future_driver_state_t *) active_gap_state,active_gatt_state; } ble_connection_slot_t;
typedef struct { unsigned adapter_generation,index,generation; } ble_connection_ref_t;
typedef struct { bool allocated,open,queue_rooted;unsigned generation,capacity,stop_reason;atomic_uint sequence,reports,dropped,malformed;uint8_t *payloads;esp32_mquickjs_native_pool_t free_slots;JSGCRef queue_ref;esp32_mquickjs_event_queue_t *queue; } ble_scanner_t;
typedef struct { unsigned generation,preferred_mtu,max_connections;void *runtime;int lock;struct { bool active; } advertiser;ble_scanner_t scanner;ble_connection_slot_t connections[1]; } ble_adapter_t;
static ble_adapter_t s_ble;
static atomic_uint s_ble_next_scanner_generation=1;
static _Atomic(esp32_mquickjs_future_driver_state_t *) s_ble_open_state,s_ble_server_notify_state;
static ble_connection_ref_t connection_ref={1,0,1};
static ble_adapter_t *ble_adapter_from_value(JSContext *ctx,JSValue v,bool check) { (void)ctx;(void)v;(void)check;return &s_ble; }
static ble_connection_slot_t *ble_connection_from_value(JSContext *ctx,JSValue v,ble_connection_ref_t **ref,bool check) { (void)ctx;(void)v;(void)check;if(ref)*ref=&connection_ref;return &s_ble.connections[0]; }
static ble_scanner_t *ble_scanner_from_value(JSContext *ctx,JSValue v,bool check) { (void)ctx;(void)v;(void)check;return &s_ble.scanner; }
static bool ble_remote_attribute_exists(ble_connection_slot_t *s,uint16_t h,void *out) { (void)s;(void)out;return h==1; }
static bool ble_has_pending_connects(void) { return false; }
static JSValue ble_throw_error(JSContext *ctx,const char *s,int a,int b,int c,int d) { (void)a;(void)b;(void)c;(void)d;return JS_ThrowInternalError(ctx,"%s",s); }
static JSValue ble_scan_event_to_js(JSContext *ctx,const void *event,void *opaque) { (void)ctx;(void)event;(void)opaque;return JS_UNDEFINED; }
static void ble_scan_event_drop(void *event,void *opaque) { (void)event;(void)opaque; }
static int queue_native;
static JSValue esp32_mquickjs_event_queue_new(JSContext *ctx,void *runtime,size_t size,unsigned capacity,int policy,JSValue (*convert)(JSContext *,const void *,void *),void (*drop)(void *,void *),void *close,void *opaque) {
    (void)runtime;(void)size;(void)capacity;(void)policy;(void)convert;(void)drop;(void)close;(void)opaque;return JS_NewObject(ctx);
}
static esp32_mquickjs_event_queue_t *esp32_mquickjs_event_queue_from_value(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return &queue_native; }
static bool esp32_mquickjs_event_queue_dispose(JSContext *ctx,JSValue v) { (void)ctx;(void)v;return true; }
static bool esp32_mquickjs_event_queue_close(void *p) { (void)p;return true; }
static bool esp32_mquickjs_event_queue_discard_all(void *p) { (void)p;return true; }
/* Intrinsic receives a rooted argv pointer, so compaction here is valid. */
static JSValue test_keys(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {
    if(step(ctx))return JS_ThrowOutOfMemory(ctx);return js_object_keys(ctx,self,argc,argv);
}
#define js_object_keys test_keys
'''
MAIN=r'''
#undef js_object_keys
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2]);int invalid=atoi(argv[3]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;inject=false;
        memset(&s_ble,0,sizeof(s_ble));s_ble.generation=s_ble.max_connections=1;s_ble.preferred_mtu=247;s_ble.connections[0].generation=1;
        JSGCRef self,args[3];*JS_AddGCRef(ctx,&self)=JS_NewObject(ctx);
        for(int i=0;i<3;i++)*JS_AddGCRef(ctx,&args[i])=JS_UNDEFINED;
        args[0].val=JS_NewInt32(ctx,1);
        if(mode==0 || mode==1 || mode==2 || mode==5 || mode==6) {
            unsigned o=mode==0 ? 0 : mode==1 ? 1 : mode==2 ? 2 : 0;
            char hole[512];memset(hole,'h',sizeof(hole));assert(!JS_IsException(JS_NewStringLen(ctx,hole,sizeof(hole))));
            args[o].val=JS_NewObject(ctx);
            JS_SetPropertyStr(ctx,args[o].val,mode==0 ? "capacity" : "timeoutMs",JS_NewInt32(ctx,invalid ? -1 : mode==0 ? 2 : 1000));
        }
        if(mode==6)JS_SetPropertyStr(ctx,args[0].val,"includeDescriptors",JS_TRUE);
        if(mode==2) { args[1].val=JS_NewArray(ctx,4);for(unsigned i=0;i<4;i++)JS_SetPropertyUint32(ctx,args[1].val,i,JS_NewInt32(ctx,i+1)); }
        if(mode==3)args[0].val=JS_NewInt32(ctx,invalid ? 65536 : 247);
        calls=0;fail_at=nth;moved_roots=0;inject=true;
        esp32_mquickjs_future_driver_state_t *state=NULL;bool ok=false;
        if(mode==0)ok=ble_scan_capture(ctx,&self,1,args,&state);
        if(mode==1)ok=ble_gatt_read_handle_capture(ctx,&self,2,args,&state);
        if(mode==2)ok=ble_gatt_write_handle_capture(ctx,&self,3,args,&state);
        if(mode==3)ok=ble_exchange_mtu_capture(ctx,&self,1,args,&state);
        if(mode==4)ok=ble_scanner_close_capture(ctx,&self,0,args,&state);
        if(mode==5)ok=ble_connection_operation_capture(ctx,&self,1,args,&state,BLE_OP_CONNECTION_CLOSE,1000);
        if(mode==6)ok=ble_discover_capture(ctx,&self,1,args,&state);
        inject=false;if(!nth)total=calls;
        if(nth || invalid) { if(ok || !JS_HasException(ctx)) {fprintf(stderr,"capture failure mode=%d nth=%d invalid=%d ok=%d\n",mode,nth,invalid,ok);return 1;}assert(!state);(void)JS_GetException(ctx); }
        else { assert(ok && state && state->owner_rooted);if(collect && mode<=2)assert(moved_roots>0);ble_future_state_release(state); }
        if(mode==6)ble_clear_remote_discovery(&s_ble.connections[0]);
        if(mode==0) {
            if(!ok)assert(!s_ble.scanner.allocated);
            ble_release_event_queue(ctx,&s_ble.scanner.queue,&s_ble.scanner.queue_ref,&s_ble.scanner.queue_rooted);
            heap_caps_free(s_ble.scanner.payloads);
        }
        for(int i=2;i>=0;i--)JS_DeleteGCRef(ctx,&args[i]);JS_DeleteGCRef(ctx,&self);JS_GC(ctx);
        assert(root_count==0 && native_live==0);JS_FreeContext(ctx);free(heap);
    }
    printf("capture mode=%d gc=%d invalid=%d boundaries=%d\n",mode,collect,invalid,total);
}
'''
class BleCaptureGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        options=(CORE/'esp32_mquickjs_options.c').read_text().replace('#include "esp32_mquickjs_options.h"',(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace('#include "esp32_mquickjs_types.h"',''))
        names=['ble_next_generation','ble_is_object','ble_to_u32','ble_validate_option_keys','ble_retain_owner','ble_ms_to_625us','ble_get_number','ble_get_bool','ble_release_event_queue','ble_parse_timeout_option','ble_future_state_storage_free','ble_future_state_release','ble_scan_capture','ble_connection_operation_capture','ble_gatt_read_handle_capture','ble_gatt_write_handle_capture','ble_exchange_mtu_capture','ble_scanner_close_capture','ble_clear_remote_discovery','ble_discover_capture']
        cls.binary=build(cls.temp.name,SDK+(CORE/'esp32_mquickjs_native_pool.c').read_text()+options+'\n'.join(extract(ble,n) for n in names),MAIN)

    def test_capture_nth_failure_and_root_release(self):
        for mode in range(7):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc),'0'])

    def test_invalid_options_do_not_leave_state_or_queue(self):
        for mode in (0,1,2,3,5,6):
            with self.subTest(mode=mode):run([str(self.binary),str(mode),'1','1'])
