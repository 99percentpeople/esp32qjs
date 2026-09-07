"""Run the production BLE status converter against the vendored moving-GC VM."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from check_js_syntax import build_checker, compiler_command, VENDOR_DIR
from test_wireless_control_regression import function

SDK = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#define BLE_STORE_OBJ_TYPE_PEER_SEC 1
#define BLE_LIFECYCLE_ACTIVE 1
static struct {
    uint8_t address[6];int own_addr_type,lifecycle;bool synchronized,role_central,role_peripheral;
    char device_name[32];uint16_t max_connections;
    struct { bool open; } connections[2];
    struct { bool active; atomic_uint dropped; } scanner;
    struct { bool active; } advertiser;
    struct { atomic_uint dropped; } server;
    atomic_uint reset_count,dropped_connection_events;
    atomic_int rejected_connection_error;
} s_ble;
static void ble_format_address(const uint8_t *addr,char *out) { (void)addr;strcpy(out,"10:20:30:40:50:60"); }
static const char *ble_address_type_name(int type) { (void)type;return "public"; }
static int ble_store_util_count(int type,int *out) { (void)type;*out=0;return 0; }
static int calls,fail_at,external_roots,moved_roots;
static JSGCRef *tracked_roots[16];
static bool collect;
static bool step(JSContext *ctx) {
    if(collect) {
        JSValue before[16];
        for(int i=0;i<external_roots;i++) before[i]=tracked_roots[i]->val;
        JS_GC(ctx);
        for(int i=0;i<external_roots;i++)
            if(before[i]!=tracked_roots[i]->val) moved_roots++;
        /* Leave an unrooted hole before the next live allocation so a later GC
         * must compact live objects. This is the actual VM, not a handle model. */
        char garbage[512];memset(garbage,'x',sizeof(garbage));
        assert(!JS_IsException(JS_NewStringLen(ctx,garbage,sizeof(garbage))));
    }
    return ++calls==fail_at;
}
static JSValue new_string(JSContext *ctx,const char *str) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewString(ctx,str);
}
static JSValue new_array(JSContext *ctx,int count) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewArray(ctx,count);
}
static JSValue new_object(JSContext *ctx) {
    if(step(ctx)) return JS_ThrowOutOfMemory(ctx);
    return JS_NewObject(ctx);
}
static JSValue set_index(JSContext *ctx,JSValue obj,uint32_t index,JSValue val) {
    if(++calls==fail_at) return JS_ThrowOutOfMemory(ctx);
    return JS_SetPropertyUint32(ctx,obj,index,val);
}
static JSValue set_property(JSContext *ctx,JSValue obj,const char *key,JSValue val) {
    if(++calls==fail_at) return JS_ThrowOutOfMemory(ctx);
    return JS_SetPropertyStr(ctx,obj,key,val);
}
static JSValue *push_root(JSContext *ctx,JSGCRef *ref) {
    assert(external_roots<16);tracked_roots[external_roots++]=ref;
    return JS_PushGCRef(ctx,ref);
}
static JSValue pop_root(JSContext *ctx,JSGCRef *ref) {
    assert(external_roots>0 && tracked_roots[external_roots-1]==ref);
    external_roots--;return JS_PopGCRef(ctx,ref);
}
#define JS_NewString new_string
#define JS_NewArray new_array
#define JS_NewObject new_object
#define JS_SetPropertyUint32 set_index
#define JS_SetPropertyStr set_property
#define JS_PushGCRef push_root
#define JS_PopGCRef pop_root
'''
MAIN = r'''
#undef JS_NewString
#undef JS_NewArray
#undef JS_NewObject
#undef JS_SetPropertyUint32
#undef JS_SetPropertyStr
#undef JS_PushGCRef
#undef JS_PopGCRef
int main(int argc,char **argv) {
    (void)argc;
    collect=atoi(argv[1])==1;
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        if(atoi(argv[1])==2) {
            JSGCRef target_ref;
            JSValue *target=JS_PushGCRef(ctx,&target_ref);
            *target=JS_NewObject(ctx);
            JSValue invalid=JS_ThrowOutOfMemory(ctx);
            assert(!esp32_mquickjs_set_property(ctx,*target,"bad",invalid));
            assert(!esp32_mquickjs_set_property_ref(ctx,target,"bad",invalid));
            assert(!esp32_mquickjs_set_property_ref(ctx,&invalid,"bad",JS_TRUE));
            assert(!esp32_mquickjs_set_property(ctx,invalid,"bad",JS_TRUE));
            assert(calls==0 && JS_HasException(ctx));
            (void)JS_GetException(ctx);
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*target,"bad")));
            JS_PopGCRef(ctx,&target_ref);
            JS_FreeContext(ctx);free(heap);
            puts("Exception values rejected before allocation-capable API boundaries");
            return 0;
        }
        calls=0;fail_at=nth;
        s_ble.role_central=true;s_ble.role_peripheral=true;
        s_ble.lifecycle=BLE_LIFECYCLE_ACTIVE;s_ble.max_connections=2;
        s_ble.rejected_connection_error=17;
        strcpy(s_ble.device_name,"test");
        JSGCRef result_ref;
        JSValue *result=JS_PushGCRef(ctx,&result_ref);
        *result=ble_adapter_status_to_js(ctx);
        assert(external_roots==0);
        if(nth==0) {
            total=calls;
            if(collect) assert(moved_roots>0);
            assert(!JS_IsException(*result) && !JS_HasException(ctx));
            JS_GC(ctx);
            JSGCRef roles_ref;
            JSValue *roles=JS_PushGCRef(ctx,&roles_ref);
            *roles=JS_GetPropertyStr(ctx,*result,"roles");
            assert(JS_IsArray(ctx,*roles));
            for(uint32_t i=0;i<2;i++) {
                JSValue role=JS_GetPropertyUint32(ctx,*roles,i);
                JSCStringBuf buf;
                const char *str=JS_ToCString(ctx,role,&buf);
                assert(str && !strcmp(str,i ? "peripheral" : "central"));
            }
            JS_PopGCRef(ctx,&roles_ref);
            JSValue code=JS_GetPropertyStr(ctx,*result,"rejectedConnectionError");
            int32_t raw;assert(!JS_ToInt32(ctx,&raw,code) && raw==17);
        } else {
            if(!JS_IsException(*result) || !JS_HasException(ctx)) {
                fprintf(stderr,"status conversion swallowed allocation failure %d/%d\n",nth,total);
                return 1;
            }
            (void)JS_GetException(ctx);
        }
        JS_PopGCRef(ctx,&result_ref);
        JS_GC(ctx);
        JS_FreeContext(ctx);free(heap);
    }
    printf("Checked %d allocation-capable API boundaries; moving GC=%d\n",total,collect);
}
'''

class BleStatusGcRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        directory=pathlib.Path(cls.temporary.name)
        headers=ROOT/'build/js-syntax'
        build_checker(headers)
        prefix=(ROOT/'scripts/mquickjs_syntax_check.c').read_text().split('#define CHECKER_HEAP_SIZE')[0]
        core=(ROOT/'components/esp32_mquickjs/src/core/esp32_mquickjs.c').read_text()
        helper=''
        for name in ('esp32_mquickjs_set_property','esp32_mquickjs_set_property_ref'):
            start=core.index('bool '+name+'(')
            helper+=core[start:core.index('\n}\n',start)+3]
        ble=(ROOT/'components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c').read_text()
        source=directory/'status.c'
        source.write_text(prefix+SDK+helper+function(ble,'ble_adapter_status_to_js')+MAIN)
        cls.binary=directory/'test'
        command=[*compiler_command(),'-O1','-D_GNU_SOURCE',f'-I{VENDOR_DIR}',f'-I{headers}',
                 f'-I{ROOT / "components/esp32_mquickjs/include"}',str(source),
                 *(str(VENDOR_DIR/name) for name in ('mquickjs.c','dtoa.c','libm.c','cutils.c')),
                 '-lm','-o',str(cls.binary)]
        result=subprocess.run(command,capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stderr)

    def test_nth_allocation_failure_returns_exception(self):
        self.run_mode(0)

    def test_property_helpers_preserve_exception_without_storing_sentinel(self):
        self.run_mode(2)

    def test_nth_allocation_failure_and_forced_moving_gc(self):
        self.run_mode(1)

    def run_mode(self,mode):
        result=subprocess.run([str(self.binary),str(mode)],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('allocation-capable API boundaries',result.stdout)
