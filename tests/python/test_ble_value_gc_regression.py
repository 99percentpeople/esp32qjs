"""Fault injection and real moving GC for production BLE value converters."""
import pathlib
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from test_ble_status_gc_regression import ROOT,SDK,build_checker,compiler_command,VENDOR_DIR,function
EXTRA=r'''
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define BLE_GATT_CHR_PROP_BROADCAST 1
#define BLE_GATT_CHR_PROP_READ 2
#define BLE_GATT_CHR_PROP_WRITE 4
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP 8
#define BLE_GATT_CHR_PROP_NOTIFY 16
#define BLE_GATT_CHR_PROP_INDICATE 32
#define BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE 64
#define BLE_CONTROL_DISCONNECTED 1
#define BLE_CONTROL_MTU 2
#define BLE_CONTROL_SECURITY 3
#define BLE_SM_IOACT_DISP 1
#define BLE_SM_IOACT_INPUT 2
#define BLE_SM_IOACT_NUMCMP 3
typedef struct { uint8_t type,val[6]; } ble_addr_t;
struct ble_gap_sec_state { bool bonded,encrypted,authenticated; };
struct ble_store_key_sec { ble_addr_t peer_addr; };
struct ble_store_value_sec { bool sc; };
static int ble_store_read_peer_sec(void *key,struct ble_store_value_sec *value) { (void)key;value->sc=true;return 0; }
#define BLE_UUID_TEXT_MAX 37
#define BLE_HS_ENOMEM 6
typedef struct { int uuid;unsigned start_handle,end_handle,first_characteristic,characteristic_count; } ble_remote_service_t;
typedef struct { int uuid;unsigned properties,declaration_handle,value_handle,first_descriptor,descriptor_count; } ble_remote_characteristic_t;
typedef struct { int uuid;unsigned handle; } ble_remote_descriptor_t;
static void ble_uuid_to_text(const int *uuid,char *out) { (void)uuid;strcpy(out,"00001800-0000-1000-8000-00805f9b34fb"); }
typedef struct { unsigned connection_index;int host_code; } esp32_mquickjs_future_driver_state_t;
static JSValue ble_throw_error(JSContext *ctx,const char *code,int h,int a,int c,int attr) {
    (void)code;(void)h;(void)a;(void)c;(void)attr;return JS_ThrowInternalError(ctx,"fixture host error");
}
typedef struct {
    unsigned service_count,characteristic_count,descriptor_count,discovery_generation;
    ble_remote_service_t services[1];ble_remote_characteristic_t characteristics[1];ble_remote_descriptor_t descriptors[1];
    bool open,central,rssi_valid; uint16_t mtu; int rssi;
    ble_addr_t peer;struct ble_gap_sec_state security;
    unsigned generation;atomic_bool terminal_pending;atomic_uint gatt_pending;_Atomic(void *) active_gatt_state;
} ble_connection_slot_t;
typedef ble_connection_slot_t ble_connection_snapshot_t;
typedef struct {
    unsigned adapter_generation,connection_generation,sequence,mtu,request_id,passkey;
    int kind,status,pairing_action;int64_t timestamp_us,expires_at_us;
} ble_connection_event_t;
static JSValue new_int64(JSContext *ctx,int64_t value) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewInt64(ctx,value);
}
#define JS_NewInt64 new_int64
'''
MAIN=r'''
#undef JS_NewString
#undef JS_NewArray
#undef JS_NewObject
#undef JS_SetPropertyUint32
#undef JS_SetPropertyStr
#undef JS_PushGCRef
#undef JS_PopGCRef
#undef JS_NewInt64
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2])!=0;
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        calls=0;fail_at=nth;moved_roots=0;
        ble_connection_slot_t slot={.open=true,.central=true,.mtu=247,.generation=1,
            .security={true,true,true},.terminal_pending=true};
        ble_connection_event_t event={.adapter_generation=1,.connection_generation=1,
            .sequence=2,.timestamp_us=INT64_C(1)<<45,.expires_at_us=INT64_C(1)<<45,
            .mtu=247,.request_id=3,.passkey=123456};
        s_ble.generation=1;
        JSGCRef result_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref);
        switch(mode) {
        case 0:*result=ble_properties_to_js(ctx,127);break;
        case 1:*result=ble_address_to_js(ctx,&slot.peer);break;
        case 2:*result=ble_security_to_js(ctx,&slot.security,&slot.peer);break;
        case 3:*result=ble_connection_status_to_js(ctx,&slot,0);break;
        case 10: {
            s_ble.connections[0]=slot;
            ble_connection_slot_t *gatt=&s_ble.connections[0];
            gatt->service_count=gatt->characteristic_count=gatt->descriptor_count=1;
            gatt->discovery_generation=3;
            gatt->services[0]=(ble_remote_service_t){.start_handle=1,.end_handle=10,.characteristic_count=1};
            gatt->characteristics[0]=(ble_remote_characteristic_t){.declaration_handle=2,.value_handle=3,.properties=127,.descriptor_count=1};
            gatt->descriptors[0]=(ble_remote_descriptor_t){.handle=4};
            esp32_mquickjs_future_driver_state_t state={0};
            *result=ble_discover_finish(ctx,&state);break;
        }
        default:
            event.kind=mode<7 ? mode-3 : 4;
            event.pairing_action=mode-6;
            *result=ble_connection_event_to_js(ctx,&event,&slot);break;
        }
        assert(!external_roots);
        if(nth==0) {
            total=calls;
            if(JS_IsException(*result) || JS_HasException(ctx)) {
                fprintf(stderr,"normal conversion failed mode=%d gc=%d\n",mode,collect);return 1;
            }
            if(collect && mode!=2) assert(moved_roots>0);
            JS_GC(ctx);
            if(mode==0) {
                assert(JS_IsArray(ctx,*result));
                static const char *names[]={"broadcast","read","write","write-without-response","notify","indicate","authenticated-signed-write"};
                for(int i=0;i<7;i++) {
                    JSCStringBuf buf;JSValue value=JS_GetPropertyUint32(ctx,*result,i);
                    const char *text=JS_ToCString(ctx,value,&buf);assert(text && !strcmp(text,names[i]));
                }
            }
            if(mode==10) {
                int32_t generation;
                assert(!JS_ToInt32(ctx,&generation,JS_GetPropertyStr(ctx,*result,"generation")) && generation==3);
                static const char *keys[]={"services","characteristics","descriptors"};
                JSGCRef array_ref;JSValue *array=JS_PushGCRef(ctx,&array_ref);
                for(int i=0;i<3;i++) {
                    *array=JS_GetPropertyStr(ctx,*result,keys[i]);
                    assert(JS_IsArray(ctx,*array));
                    int32_t length;
                    assert(!JS_ToInt32(ctx,&length,JS_GetPropertyStr(ctx,*array,"length")) && length==1);
                    assert(JS_GetClassID(ctx,JS_GetPropertyUint32(ctx,*array,0))>=0);
                }
                JS_PopGCRef(ctx,&array_ref);
            }
            if(mode==4) assert(!slot.terminal_pending);
        } else {
            if(!JS_IsException(*result) || !JS_HasException(ctx)) {
                fprintf(stderr,"failure swallowed mode=%d nth=%d/%d gc=%d\n",mode,nth,total,collect);return 1;
            }
            if(mode==4) assert(slot.terminal_pending);
            (void)JS_GetException(ctx);
        }
        JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);JS_FreeContext(ctx);free(heap);
    }
    printf("mode=%d checked %d allocation boundaries; gc=%d\n",mode,total,collect);
}
'''
class BleValueGcRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        directory=pathlib.Path(cls.temp.name);headers=ROOT/'build/js-syntax';build_checker(headers)
        prefix=(ROOT/'scripts/mquickjs_syntax_check.c').read_text().split('#define CHECKER_HEAP_SIZE')[0]
        core=(ROOT/'components/esp32_mquickjs/src/core/esp32_mquickjs.c').read_text()
        start=core.index('bool esp32_mquickjs_set_property_ref(')
        helper=core[start:core.index('\n}\n',start)+3]
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        bodies='\n'.join(function(ble,n) for n in ['ble_address_to_js','ble_security_to_js',
            'ble_connection_snapshot','ble_connection_event_to_js','ble_properties_to_js','ble_connection_status_to_js',
            'ble_service_index_for_characteristic','ble_characteristic_index_for_descriptor','ble_discovery_service_record',
            'ble_discovery_characteristic_record','ble_discovery_descriptor_record','ble_discover_finish'])
        source=directory/'values.c'
        sdk=SDK.replace('uint8_t address[6];','int lock;unsigned generation;uint8_t address[6];').replace(
            'struct { bool open; } connections[2];','ble_connection_slot_t connections[2];')
        split=EXTRA.index('static JSValue new_int64')
        source.write_text(prefix+'\n#include <stdbool.h>\n#include <stdatomic.h>\n'+EXTRA[:split]+sdk+EXTRA[split:]+helper+bodies+MAIN)
        cls.binary=directory/'test'
        result=subprocess.run([*compiler_command(),'-O1','-D_GNU_SOURCE',f'-I{VENDOR_DIR}',f'-I{headers}',
            f'-I{ROOT / "components/esp32_mquickjs/include"}',str(source),
            *(str(VENDOR_DIR/n) for n in ['mquickjs.c','dtoa.c','libm.c','cutils.c']),'-lm','-o',str(cls.binary)],capture_output=True,text=True)
        if result.returncode:raise AssertionError(result.stderr)

    def run_case(self,mode,gc):
        result=subprocess.run([str(self.binary),str(mode),str(int(gc))],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_gatt_properties_nth_failure(self):self.run_case(0,False)
    def test_gatt_properties_moving_gc(self):self.run_case(0,True)
    def test_address_security_connection_status_and_control_events(self):
        for mode in range(1,10):
            for gc in [False,True]:
                with self.subTest(mode=mode,gc=gc):self.run_case(mode,gc)

    def test_gatt_snapshot_allocation_failure_and_moving_gc(self):
        for gc in [False,True]:
            with self.subTest(gc=gc):self.run_case(10,gc)
