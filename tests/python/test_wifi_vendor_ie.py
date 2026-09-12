"""Deferred production Vendor IE ownership/configuration; no fixture run this wave.

SDK bytes, lock and Radio storage are injected. Exact production registry acquire,
release, lifecycle admission and all Vendor IE operations are extracted unchanged.
No RF, SDK allocation or runtime consumer teardown proof is inferred.
"""
import re
import tempfile
import unittest
from test_wifi_config_controls import sdk_types, structure
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run
from test_wifi_rx_target import unit


def vendor_code(profile, ap=True, enterprise=False):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    vendor = (COMPONENT / 'internal/esp32_mquickjs_wifi_vendor_ie.h').read_text()
    code = PRELUDE + f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n'
    if enterprise:
        code += '#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1\n'
        start = radio.index('#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT\n/* Operation mutex only.')
        code += radio[start:radio.index('\n#endif', start) + len('\n#endif')] + '\n'
    code += sdk_types(profile, ('wifi_vendor_ie_type_t', 'wifi_vendor_ie_id_t'))
    for name in ('esp32_mquickjs_wifi_radio_client_t', 'esp32_mquickjs_wifi_radio_driver_state_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    for name in ('esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_lifecycle_t',
                 'esp32_mquickjs_wifi_radio_promiscuous_lease_t', 'esp32_mquickjs_wifi_radio_configuration_selection_t'):
        code += structure(header, name)
    # Unused broker storage is an injected boundary, never exercised here.
    code += 'typedef unsigned esp32_mquickjs_wifi_promiscuous_token_t;\n'
    code += structure(radio, 'wifi_radio_live_lease_t')
    code += '\n' + '\n'.join(re.findall(r'^#define ESP32_MQUICKJS_WIFI_VENDOR_IE_.*$', vendor, re.M)) + '\n'
    for name in ('esp32_mquickjs_wifi_vendor_ie_slot_t', 'esp32_mquickjs_wifi_vendor_ie_status_t'):
        code += structure(vendor, name)
    code += BOUNDARIES
    code += re.search(r'static struct \{[^}]*\} s_vendor_ie;', radio).group(0)
    code += (COMPONENT / 'internal/esp32_mquickjs_wifi_radio_mutation.h').read_text()
    for name in ('wifi_radio_lease_valid', 'wifi_radio_promiscuous_owner', 'wifi_radio_acquire_locked',
                 'wifi_radio_release_locked', 'wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
                 'wifi_radio_vendor_ie_interface', 'wifi_radio_vendor_ie_release_empty',
                 'wifi_radio_vendor_ie_start_matches', 'wifi_radio_vendor_ie_remove',
                 'wifi_radio_vendor_ie_start_count', 'wifi_radio_vendor_ie_start_park',
                 'wifi_radio_vendor_ie_begin_start_locked', 'wifi_radio_vendor_ie_start_attach',
                 'wifi_radio_vendor_ie_start_commit', 'esp32_mquickjs_wifi_radio_vendor_ie_clear_lifecycle',
                 'esp32_mquickjs_wifi_radio_vendor_ie_set',
                 'esp32_mquickjs_wifi_radio_vendor_ie_clear', 'esp32_mquickjs_wifi_radio_vendor_ie_status'):
        code += extract(radio, name)
    return code + RESET


class WiFiVendorIe(unittest.TestCase):
    def test_slots_failed_enable_cleanup_suffix_and_exact_lifecycle_owners(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(target=profile, softap=ap):
                    compile_run(self, vendor_code(profile, ap) + MAIN)

    def test_public_input_capture_result_oom_and_rooted_status(self):
        code = vendor_code('esp32c5/representative')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += 'static const char *esp_err_to_name(int err) {(void)err;return "injected";}\n'
        code += 'static JSValue esp32_mquickjs_wifi_vendor_ie_watch_status(JSContext *ctx) {return JS_NewObject(ctx); }\n'
        code += unit(COMPONENT / 'src/modules/wifi_vendor_ie/esp32_mquickjs_wifi_vendor_ie.c')
        base = '{interface:"station",frame:"probe-request",index:0,enabled:true,data:[221,4,1,2,3,4]}'
        cases = [(base, True), ('null', False), (base.replace('index:0','index:1.5'), False),
                 (base.replace('index:0','index:"0"'), False), (base.replace('enabled:true','enabled:1'), False),
                 (base.replace('probe-request','beacon'), False), (base.replace('[221,4,1,2,3,4]','[221,5,1,2,3,4]'), False),
                 (base.replace('[221,4,1,2,3,4]','[221,4,1,2,3,256]'), False),
                 (base.replace('interface:', 'extra:1,interface:'), False),
                 (base.replace('enabled:true','enabled:false'), False)]
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in cases:
                run([str(binary), "(" + expression + ")", str(int(valid)), 'set'])
            for scenario in ('arity', 'sdk-error', 'status', 'clear'):
                run([str(binary), "(" + base + ")", '0' if scenario in ('arity', 'sdk-error') else '1', scenario])


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_NOT_SUPPORTED -3
#define ESP_ERR_WIFI_NOT_INIT -4
#define ESP_ERR_NO_MEM -5
#define WIFI_RADIO_MAX_LEASES 12
#define WIFI_VENDOR_IE_ELEMENT_ID 221
/* These isolated Vendor/EAP cases contain no NAN or Mesh native owner. */
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
typedef int esp_err_t;
'''

BOUNDARIES = r'''
static struct {
    int lock;uint32_t generation,next_lease_identity,next_lifecycle_identity,wake_locks;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;esp_err_t fault_error;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;wifi_storage_t storage;
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    struct {unsigned identity,lease_identity;} operation;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
} s_radio;
static struct {struct {unsigned identity,owner_identity,generation;} owner;bool uncertain;} s_interval;
static struct {unsigned identity,generation;} s_tx_rate_lease;
static unsigned locks,critical,native_calls,fail_call,invalidations;
static unsigned removal_calls[10];
static uint8_t native_ie[10][257];
static uint16_t native_length[10];
static void (*on_lock)(void);
static void wifi_radio_operation_lock(void) {
    assert(!locks && !critical);locks=1;
    if(on_lock){void (*f)(void)=on_lock;on_lock=NULL;f();}
}
static void wifi_radio_operation_unlock(void) {assert(locks && !critical);locks=0;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(locks && !critical);critical=1;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(locks && critical);critical=0;} while(0)
static void wifi_radio_invalidate_stop_snapshot_locked(void) {assert(locks && !critical);++invalidations;}
static void wifi_radio_release_promiscuous_locked(esp32_mquickjs_wifi_radio_promiscuous_lease_t *p) {(void)p;assert(0);}
static int esp_wifi_set_vendor_ie(bool enabled,wifi_vendor_ie_type_t frame,wifi_vendor_ie_id_t index,const void *data) {
    assert(locks && !critical && (unsigned)frame<5 && (unsigned)index<2);++native_calls;
    unsigned slot=(unsigned)frame*2U+index;
    if(enabled) {assert(data);const uint8_t *bytes=data;native_length[slot]=bytes[1]+2U;memcpy(native_ie[slot],bytes,native_length[slot]);}
    else {++removal_calls[slot];native_length[slot]=0;}
    /* Deliberately mutate even on an error: retained owner cannot assume absence. */
    return native_calls==fail_call ? 77 : ESP_OK;
}
'''

RESET = r'''
static void reset_vendor(void) {
    assert(!locks && !critical);memset(&s_radio,0,sizeof(s_radio));memset(&s_vendor_ie,0,sizeof(s_vendor_ie));
    memset(&s_interval,0,sizeof(s_interval));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
    memset(native_length,0,sizeof(native_length));memset(removal_calls,0,sizeof(removal_calls));
    native_calls=fail_call=invalidations=0;on_lock=NULL;
    s_radio.generation=7;s_radio.next_lease_identity=s_radio.next_lifecycle_identity=1;
    s_radio.started=s_radio.driver_owned=s_radio.storage_configured=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.effective_mode=CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? WIFI_MODE_APSTA : WIFI_MODE_STA;
}
static const uint8_t element[]={221,4,1,2,3,4};
static int put(unsigned frame,unsigned index) {
    return esp32_mquickjs_wifi_radio_vendor_ie_set(wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)frame),
        (wifi_vendor_ie_type_t)frame,index,true,element,sizeof(element));
}
static unsigned owners(void) {
    esp32_mquickjs_wifi_vendor_ie_status_t status;esp32_mquickjs_wifi_radio_vendor_ie_status(&status);return status.owners;
}
static int lifecycle(void) {
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};wifi_radio_operation_lock();
    int err=wifi_radio_begin_lifecycle_locked(NULL,NULL,NULL,&token);wifi_radio_operation_unlock();return err;
}
static void late_lifecycle(void) {s_radio.lifecycle.identity=999;}
'''

MAIN = r'''
int main(void) {
    reset_vendor();s_radio.driver_owned=false;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !native_calls);
    assert(put(1,0)==ESP_ERR_WIFI_NOT_INIT && !owners());
    reset_vendor();on_lock=late_lifecycle;assert(put(1,0)==ESP_ERR_INVALID_STATE && !native_calls && !owners());
    reset_vendor();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(put(1,0)==ESP_OK && owners()==1);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    reset_vendor();s_radio.next_lease_identity=0;assert(put(1,0)==ESP_ERR_NO_MEM && !native_calls);
    reset_vendor();for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)s_radio.leases[i].identity=i+100;
    assert(put(1,0)==ESP_ERR_NO_MEM && !native_calls);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    reset_vendor();esp32_mquickjs_wifi_radio_lease_t ap={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==ESP_OK);
    wifi_radio_operation_unlock();
    assert(put(0,0)==ESP_OK && put(1,0)==ESP_OK && owners()==2);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    wifi_radio_operation_lock();assert(wifi_radio_lease_valid(&ap));wifi_radio_release_locked(&ap);wifi_radio_operation_unlock();
#endif
    reset_vendor();assert(put(1,0)==ESP_OK && owners()==1 && invalidations==1);
    assert(lifecycle()==ESP_ERR_INVALID_STATE);unsigned before=native_calls;
    assert(put(1,0)==ESP_ERR_INVALID_STATE && native_calls==before);
    assert(put(3,1)==ESP_OK && owners()==1);
    fail_call=native_calls+1;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==77 && owners()==1);
    assert(removal_calls[2]==1 && removal_calls[7]==1 && s_vendor_ie.slots[2].pending && !s_vendor_ie.slots[7].length);
    fail_call=0;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    assert(removal_calls[2]==2 && removal_calls[7]==1 && lifecycle()==ESP_OK);
    reset_vendor();fail_call=1;assert(put(1,0)==77 && owners()==1 && s_vendor_ie.slots[2].pending);
    assert(lifecycle()==ESP_ERR_INVALID_STATE);fail_call=0;
    assert(put(1,0)==ESP_ERR_INVALID_STATE && native_calls==1);
    s_radio.fault_stage="unrelated";s_radio.fault_error=88;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners() && s_radio.fault_error==88);
    reset_vendor();assert(put(1,0)==ESP_OK);esp32_mquickjs_wifi_radio_lease_t old=s_vendor_ie.leases[0];
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);assert(put(1,0)==ESP_OK);
    wifi_radio_operation_lock();wifi_radio_release_locked(&old);wifi_radio_operation_unlock();assert(owners()==1);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);
    reset_vendor();
    for(unsigned frame=0;frame<5;++frame) for(unsigned index=0;index<2;++index) {
        int expected=!CONFIG_ESP_WIFI_SOFTAP_SUPPORT && wifi_radio_vendor_ie_interface((wifi_vendor_ie_type_t)frame)==WIFI_IF_AP ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
        assert(put(frame,index)==expected);
    }
    assert(owners()==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2 : 1));
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(WIFI_IF_STA)==ESP_OK);
    assert(owners()==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 1 : 0));
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
    reset_vendor();uint8_t maximum[257]={221,255,1,2,3,4};
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,0,true,maximum,257)==ESP_OK);
    memset(maximum,0,sizeof(maximum));assert(native_ie[2][0]==221 && native_length[2]==257);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK);
    before=native_calls;
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,0,true,element,5)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_BEACON,0,true,element,6)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,2,true,element,6)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_radio_vendor_ie_set(WIFI_IF_STA,WIFI_VND_IE_TYPE_PROBE_REQ,0,false,element,6)==ESP_ERR_INVALID_ARG);
    assert(native_calls==before && !owners() && !locks && !critical);return 0;
}
'''


VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"vendor-ie",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        reset_vendor();
        bool query=!strcmp(argv[3],"status"),clear=!strcmp(argv[3],"clear"),arity=!strcmp(argv[3],"arity");
        bool failed=!strcmp(argv[3],"sdk-error");
        if(query || clear)assert(put(1,0)==ESP_OK);
        if(failed)fail_call=1;
        calls=0;fail_at=nth;collect=true;inject=true;
        if(query)*output=js_wifi_vendor_ie_status(ctx,NULL,0,NULL);
        else if(clear)*output=js_wifi_vendor_ie_clear(ctx,NULL,0,NULL);
        else *output=js_wifi_vendor_ie_set(ctx,NULL,arity ? 0 : 1,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity || (!expected && !failed))assert(!native_calls && !owners());
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==77 && owners()==1);
            }
        } else if(!clear) {
            *input=JS_GetPropertyStr(ctx,*output,"owners");int32_t n;
            assert(!JS_ToInt32(ctx,&n,*input) && n==1);
        }
        /* OOM after SDK dispatch does not undo the write or silently resubmit. */
        if(!clear && native_calls)assert(native_calls==1 && owners()==1);
        fail_call=0;assert(esp32_mquickjs_wifi_radio_vendor_ie_clear(-1)==ESP_OK && !owners());
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical);free(heap);
    }
    return 0;
}
'''
