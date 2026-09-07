"""Production wireless payload converters, native pools and real VM ownership."""
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from wireless_vm_fixture import ROOT,CORE,build,extract,run

SDK=r'''
#include "esp32_mquickjs_native_pool.h"
#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1
#define BLE_ADDR_PUBLIC_ID 2
#define BLE_ADDR_RANDOM_ID 3
#define BLE_HS_ADV_MAX_SZ 31
#define CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES 64
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP 4
#define BLE_HCI_ADV_RPT_EVTYPE_DIR_IND 1
#define BLE_HCI_ADV_RPT_EVTYPE_ADV_IND 0
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND 2
#define BLE_SERVER_EVENT_WRITE 1
#define ESPNOW_ADDRESS_BYTES 6
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 0
static const uint8_t s_broadcast_address[6]={255,255,255,255,255,255};
typedef struct { uint8_t type,val[6]; } ble_addr_t;
typedef struct { unsigned generation,capacity;uint8_t *payloads;esp32_mquickjs_native_pool_t free_slots; } ble_scanner_t;
typedef ble_scanner_t ble_subscription_t;
typedef struct { unsigned generation,pool_index,length,sequence,event_type;int rssi;int64_t timestamp_us;ble_addr_t peer; } ble_scan_event_t;
typedef struct { unsigned subscription_generation,pool_index,length,sequence;int64_t timestamp_us;bool indication; } ble_notification_event_t;
typedef struct { unsigned generation;bool open,allocated;uint16_t conn_handle;JSGCRef queue_ref; } ble_connection_slot_t;
typedef struct { unsigned adapter_generation,index,generation; } ble_connection_ref_t;
static ble_connection_ref_t s_ble_closed_connection_ref;
static struct { unsigned generation,max_connections;ble_connection_slot_t connections[1]; } s_ble;
static unsigned orphans;
static void ble_request_orphan_close(unsigned generation) {assert(generation==s_ble.generation);orphans++;}
typedef struct { unsigned event_capacity,characteristic_count;uint8_t *event_payloads;esp32_mquickjs_native_pool_t free_slots;struct { const char *id; } characteristics[1]; } ble_gatt_server_t;
typedef struct { unsigned adapter_generation,characteristic_index,conn_handle,kind,pool_index,length,sequence,offset;int64_t timestamp_us;bool notify,indicate; } ble_server_event_t;
typedef struct { unsigned generation,sequence,length;int64_t timestamp_us;int rssi;unsigned channel;uint8_t source[6],destination[6];uint8_t payload[64]; } espnow_rx_slot_t;
typedef struct { unsigned generation,receive_capacity,max_payload_bytes;espnow_rx_slot_t *rx_slots;esp32_mquickjs_native_pool_t rx_free; } espnow_session_t;
typedef struct { unsigned generation,slot_index; } espnow_receive_event_t;
static void *esp32_mquickjs_memory_payload_alloc(const char *name,size_t n,int policy) { (void)name;return heap_caps_malloc(n,policy); }
'''
MAIN=r'''
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]),gc=atoi(argv[2]),invalid=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        inject=false;collect=gc;calls=0;fail_at=nth;moved_roots=0;orphans=0;
        uint8_t bytes[64]={1,2,3,4};
        ble_scanner_t scanner={.generation=1,.capacity=1,.payloads=bytes};
        ble_subscription_t subscription=scanner;
        ble_gatt_server_t server={.event_capacity=1,.characteristic_count=1,.event_payloads=bytes,.characteristics={{"value"}}};
        espnow_rx_slot_t slot={.generation=1,.length=4,.sequence=2,.timestamp_us=INT64_C(1)<<45,.payload={1,2,3,4}};
        espnow_session_t session={.generation=1,.receive_capacity=1,.max_payload_bytes=64,.rx_slots=&slot};
        ble_scan_event_t scan_event={.generation=1,.length=4,.timestamp_us=INT64_C(1)<<45};
        ble_notification_event_t notify_event={.subscription_generation=1,.length=4,.timestamp_us=INT64_C(1)<<45};
        ble_server_event_t server_event={.adapter_generation=1,.kind=BLE_SERVER_EVENT_WRITE,.length=4,.timestamp_us=INT64_C(1)<<45};
        espnow_receive_event_t rx_event={.generation=1};
        s_ble.generation=s_ble.max_connections=1;
        s_ble.connections[0]=(ble_connection_slot_t){.generation=1,.open=true,.allocated=!invalid,.queue_ref={.val=JS_UNDEFINED}};
        esp32_mquickjs_native_pool_t *pool=mode==0 ? &scanner.free_slots : mode==1 ? &subscription.free_slots : mode==2 ? &server.free_slots : &session.rx_free;
        assert(esp32_mquickjs_native_pool_init(pool,1));uint16_t index;assert(esp32_mquickjs_native_pool_acquire(pool,&index));
        JSGCRef result_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref);
        inject=true;
        if(mode==0)*result=ble_scan_event_to_js(ctx,&scan_event,&scanner);
        if(mode==1)*result=ble_notification_event_to_js(ctx,&notify_event,&subscription);
        if(mode==2)*result=ble_server_event_to_js(ctx,&server_event,&server);
        if(mode==3)*result=espnow_receive_event_to_js(ctx,&rx_event,&session);
        inject=false;if(nth==0)total=calls;
        if(nth || invalid) {
            assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx);
        } else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));
            if(gc)assert(moved_roots>0);
            JSValue data=JS_GetPropertyStr(ctx,*result,"data");const uint8_t *p;size_t n;
            assert(esp32_mquickjs_byte_view_acquire_read(ctx,data,"test",&p,&n));assert(n==4 && !memcmp(p,bytes,4));
            esp32_mquickjs_byte_view_release_read(ctx,data);
        }
        if(!esp32_mquickjs_native_pool_acquire(pool,&index)) {fprintf(stderr,"pool leaked mode=%d invalid=%d nth=%d\n",mode,invalid,nth);return 1;}
        assert(!esp32_mquickjs_native_pool_acquire(pool,&index));
        JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);
        if(mode==2 && (nth || invalid) && orphans) {fprintf(stderr,"failed conversion closed adapter nth=%d\n",nth);return 1;}
        assert(root_count==0 && native_live==0);
        JS_FreeContext(ctx);free(heap);
    }
    printf("payload mode=%d gc=%d invalid=%d boundaries=%d\n",mode,gc,invalid,total);
}
'''
class WirelessPayloadGc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        espnow=(ROOT/'components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c').read_text()
        extra=(CORE/'esp32_mquickjs_native_pool.c').read_text()+SDK
        extra+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_format_address')
        extra+='\n'.join(extract(ble,n) for n in ['ble_address_type_name','ble_format_address','ble_address_to_js','ble_find_connection','ble_retire_handle','ble_new_connection_handle','ble_scan_pool_release','ble_scan_event_to_js','ble_notification_pool_release','ble_notification_event_to_js','ble_server_event_pool_release','ble_server_event_to_js','js_ble_connection_finalizer'])
        extra+='\n'.join(extract(espnow,n) for n in ['espnow_format_address','espnow_release_rx_slot','espnow_receive_event_to_js'])
        classes='static const JSClassDef connection_class=JS_CLASS_DEF("BLEConnection",0,js_byte_view_constructor,JS_CLASS_BLE_CONNECTION,NULL,NULL,NULL,js_ble_connection_finalizer);'
        cls.binary=build(cls.temp.name,extra,MAIN,classes,'JS_PROP_CLASS_DEF("BLEConnection", &connection_class),',
                         '#define JS_CLASS_BLE_CONNECTION (JS_CLASS_USER+34)\nvoid js_ble_connection_finalizer(JSContext *,void *);')

    def test_payload_copy_view_event_failure_and_gc(self):
        for mode in range(4):
            for gc in (0,1):
                with self.subTest(mode=mode,gc=gc):run([str(self.binary),str(mode),str(gc),'0'])

    def test_server_write_dequeued_after_connection_disappears(self):
        run([str(self.binary),'2','1','1'])
