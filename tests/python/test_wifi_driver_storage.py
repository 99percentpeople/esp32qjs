"""Deferred production storage selection and explicit repair; SDK/lock/VM injection only."""
import re
import tempfile
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def storage_code(profile, ap=True):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = control_code(profile, ap)
    code = code.replace('wifi_mode_t effective_mode;',
                        'wifi_mode_t effective_mode;bool stop_required;')
    code = code.replace('static struct {unsigned identity;} s_tx_rate_lease;',
                        'static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;')
    return code + BOUNDARIES + extract(source, 'esp32_mquickjs_wifi_radio_set_storage') + RESET


class WiFiDriverStorage(unittest.TestCase):
    def test_production_admission_uncertainty_and_explicit_replacement(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, storage_code(profile, ap) + MAIN)

    def test_public_strict_strings_arity_error_gc_and_no_result_allocation(self):
        code = re.sub(r'\bcalls\b', 'native_calls', storage_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('driver_phy_write_error', 'js_wifi_driver_set_storage'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('"ram"', True), ('"flash"', True), ('"RAM"', False),
                                      ('"ram\\u0000extra"', False), ('"flash "', False),
                                      ('0', False), ('true', False), ('null', False), ('{}', False)]:
                run([str(binary), expression, str(int(valid)), 'normal'])
            for scenario in ('arity-zero', 'arity-two', 'sdk-error'):
                run([str(binary), '"flash"', '0', scenario])


BOUNDARIES = r'''
static wifi_storage_t native_storage;
static void (*storage_hook)(void);
static int esp_wifi_set_storage(wifi_storage_t requested) {
    int err=sdk_step(true);native_storage=requested; /* Partial mutation even on error. */
    if(storage_hook)storage_hook();return err;
}
'''

RESET = r'''
static void storage_reset(void) {
    reset();s_radio.started=s_radio.stop_required=false;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.storage=native_storage=WIFI_STORAGE_RAM;
    memset(s_radio.leases,0,sizeof(s_radio.leases));s_tx_rate_lease.restore_pending=false;
    storage_hook=NULL;
}
'''

MAIN = r'''
#define SELECT(value) esp32_mquickjs_wifi_radio_set_storage(value,&result)
int main(void) {
    storage_reset();assert(SELECT((wifi_storage_t)99)==ESP_ERR_INVALID_ARG && !calls && !writes);
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_FLASH,NULL)==ESP_ERR_INVALID_ARG && !calls);
    for(unsigned invalid=0;invalid<13;++invalid) {
        storage_reset();
        if(invalid==0)s_radio.driver_owned=false;
        if(invalid==1)s_radio.storage_configured=false;
        if(invalid==2)s_radio.started=true;
        if(invalid==3)s_radio.stop_required=true;
        if(invalid==4)s_radio.restart_required=true;
        if(invalid==5)s_radio.lifecycle.identity=1;
        if(invalid==6)s_radio.operation.identity=1;
        if(invalid==7)s_radio.wake_locks=1;
        if(invalid==8)s_radio.promiscuous_claimed=true;
        if(invalid==9)s_tx_rate_lease.restore_pending=true;
        if(invalid==10)s_radio.fault_stage="other-fault";
        if(invalid==11)s_radio.cleanup_stage="cleanup";
        if(invalid==12)s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;
        assert(SELECT(WIFI_STORAGE_FLASH)==(invalid==0 ? ESP_ERR_WIFI_NOT_INIT : ESP_ERR_INVALID_STATE));
        assert(!calls && !writes && !result.mutation_attempted);
    }
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i) {
        storage_reset();s_radio.leases[i].identity=i+1;
        assert(SELECT(WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && !calls);
    }
    storage_reset();wifi_mode_t mode=s_radio.effective_mode;
    assert(SELECT(WIFI_STORAGE_FLASH)==ESP_OK && calls==1 && writes==1);
    assert(s_radio.storage_configured && s_radio.storage==WIFI_STORAGE_FLASH && native_storage==WIFI_STORAGE_FLASH);
    assert(SELECT(WIFI_STORAGE_FLASH)==ESP_OK && calls==2); /* Explicit acceptance, no invented getter/deduplication. */
    assert(s_radio.effective_mode==mode && s_radio.generation==7 && !s_radio.started);
    storage_reset();fail_at=1;
    assert(SELECT(WIFI_STORAGE_FLASH)==77 && calls==1 && writes==1 && result.mutation_attempted);
    assert(!s_radio.storage_configured && s_radio.storage==WIFI_STORAGE_RAM && native_storage==WIFI_STORAGE_FLASH);
    assert(s_radio.driver_owned && s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_FAULTED);
    assert(s_radio.fault_error==77 && !strcmp(s_radio.fault_stage,"storage-write"));
    assert(!result.rollback_attempted && !result.persistent_mutation_possible);
    fail_at=2;assert(SELECT(WIFI_STORAGE_RAM)==77 && calls==2 && !s_radio.storage_configured);
    s_radio.cleanup_stage="pending";assert(SELECT(WIFI_STORAGE_FLASH)==ESP_ERR_INVALID_STATE && calls==2);
    s_radio.cleanup_stage=NULL;fail_at=0;
    assert(SELECT(WIFI_STORAGE_FLASH)==ESP_OK && calls==3 && s_radio.storage_configured);
    assert(!s_radio.fault_stage && s_radio.fault_error==ESP_OK && s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED);
    assert(!result.rollback_attempted && s_radio.generation==7 && !s_radio.started);
    assert(!locks && !critical && !helper_locks);return 0;
}
'''

VM_MAIN = r'''
static void storage_gc(void) {JS_GC(test_ctx);}
int main(int argc,char **argv) {
    assert(argc==4);unsigned total=0;bool expected=atoi(argv[2]);
    bool failed=!strcmp(argv[3],"sdk-error");
    int arity=!strcmp(argv[3],"arity-zero") ? 0 : !strcmp(argv[3],"arity-two") ? 2 : 1;
    for(unsigned nth=0;nth<=total;++nth) {
        uint8_t *heap=malloc(96*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"storage",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        storage_reset();storage_hook=storage_gc;if(failed)native_fail_at=1;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=js_wifi_driver_set_storage(ctx,NULL,arity,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity!=1 || (!expected && !failed))assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==77);
                assert(!s_radio.storage_configured && native_calls==1);
            }
        } else {
            JSCStringBuf buffer;const char *text=JS_ToCString(ctx,*output,&buffer);assert(text);
            assert(!strcmp(text,native_storage==WIFI_STORAGE_RAM ? "ram" : "flash"));
            assert(native_calls==1 && s_radio.storage_configured && !calls); /* No JS allocation after SDK success. */
        }
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
