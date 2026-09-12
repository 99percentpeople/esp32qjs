"""Deferred real stopped-mode transaction and public binding; SDK/VM boundaries injected."""
import re
import tempfile
import unittest
from test_wifi_driver_storage import storage_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def mode_code(profile, ap=True):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    return storage_code(profile, ap) + BOUNDARIES + extract(source, 'esp32_mquickjs_wifi_radio_set_mode') + RESET


class WiFiDriverMode(unittest.TestCase):
    def test_production_admission_readback_rollback_and_persistence_diagnostics(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, mode_code(profile, ap) + MAIN)

    def test_public_mode_strings_arity_original_errors_and_gc(self):
        code = re.sub(r'\bcalls\b', 'native_calls', mode_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('driver_phy_write_error', 'js_wifi_driver_set_mode'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('"off"', True), ('"station"', True), ('"softAP"', True),
                                      ('"station+softAP"', True), ('"ap"', False), ('"apsta"', False),
                                      ('"none"', False), ('"nan"', False), ('"Station"', False),
                                      ('"station\\u0000suffix"', False), ('0', False), ('true', False), ('{}', False)]:
                run([str(binary), expression, str(int(valid)), 'normal'])
            for scenario in ('arity-zero', 'arity-two', 'sdk-error'):
                run([str(binary), '"station"', '0', scenario])


BOUNDARIES = r'''
static wifi_mode_t native_mode;
static bool corrupt_mode_readback,corrupt_mode_rollback;
static void (*mode_hook)(void);
static int esp_wifi_get_mode(wifi_mode_t *out) {
    *out=native_mode;
    if(corrupt_mode_readback && result.mutation_attempted && !result.rollback_attempted)*out=WIFI_MODE_APSTA;
    if(corrupt_mode_rollback && result.rollback_attempted)*out=WIFI_MODE_APSTA;
    int err=sdk_step(false);if(mode_hook)mode_hook();return err;
}
static int esp_wifi_set_mode(wifi_mode_t mode) {
    assert(!s_radio.started && !s_radio.stop_required);
    native_mode=mode;int err=sdk_step(true); /* Accepted mutation can precede SDK error. */
    if(mode_hook)mode_hook();return err;
}
'''

RESET = r'''
static void mode_reset(void) {
    storage_reset();s_radio.effective_mode=native_mode=WIFI_MODE_NULL;
    corrupt_mode_readback=corrupt_mode_rollback=false;mode_hook=NULL;
}
'''

MAIN = r'''
#define SELECT(mode) esp32_mquickjs_wifi_radio_set_mode(mode,&result)
int main(void) {
    mode_reset();assert(SELECT((wifi_mode_t)99)==ESP_ERR_INVALID_ARG && !calls);
    assert(esp32_mquickjs_wifi_radio_set_mode(WIFI_MODE_STA,NULL)==ESP_ERR_INVALID_ARG && !calls);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(SELECT(WIFI_MODE_AP)==ESP_ERR_NOT_SUPPORTED && !calls && !result.mutation_attempted);
    assert(SELECT(WIFI_MODE_APSTA)==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    for(unsigned previous=0;previous<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 4U : 2U);++previous) {
        for(unsigned requested=0;requested<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 4U : 2U);++requested) {
            mode_reset();s_radio.effective_mode=native_mode=(wifi_mode_t)previous;
            assert(SELECT((wifi_mode_t)requested)==ESP_OK);
            assert(s_radio.effective_mode==(wifi_mode_t)requested && native_mode==(wifi_mode_t)requested);
            assert(calls==(previous==requested ? 1U : 3U) && writes==(previous==requested ? 0U : 1U));
            assert(s_radio.generation==7 && !s_radio.started && s_radio.storage==WIFI_STORAGE_RAM);
        }
    }
    for(unsigned invalid=0;invalid<14;++invalid) {
        mode_reset();
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
        if(invalid==13)s_radio.storage=(wifi_storage_t)99;
        assert(SELECT(WIFI_MODE_STA)==(invalid==0 ? ESP_ERR_WIFI_NOT_INIT : ESP_ERR_INVALID_STATE));
        assert(!calls && !writes && !result.mutation_attempted);
    }
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i) {
        mode_reset();s_radio.leases[i].identity=i+1;
        assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !calls);
    }
    for(unsigned failure=1;failure<=3;++failure) {
        mode_reset();fail_at=failure;
        assert(SELECT(WIFI_MODE_STA)==77 && result.error==77);
        assert(native_mode==WIFI_MODE_NULL && s_radio.effective_mode==WIFI_MODE_NULL);
        assert(result.mutation_attempted==(failure>1) && result.rollback_complete==(failure>1));
        assert(!s_radio.fault_stage && !s_radio.cleanup_stage && !result.persistent_mutation_possible);
    }
    for(unsigned failure=1;failure<=2;++failure) {
        mode_reset();fail_at=3;rollback_fail=failure;
        assert(SELECT(WIFI_MODE_STA)==77 && result.error==77 && result.rollback_error==88);
        assert(!result.rollback_complete && !strcmp(s_radio.cleanup_stage,"mode-rollback"));
        assert(s_radio.fault_error==77 && s_radio.cleanup_error==88);
        unsigned before=calls;fail_at=0;
        assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
    }
    mode_reset();corrupt_mode_readback=true;
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && result.rollback_complete && native_mode==WIFI_MODE_NULL);
    assert(!s_radio.fault_stage && s_radio.effective_mode==WIFI_MODE_NULL);
    mode_reset();corrupt_mode_readback=corrupt_mode_rollback=true;
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && !result.rollback_complete);
    assert(result.rollback_error==ESP_ERR_INVALID_RESPONSE && s_radio.cleanup_stage);
    mode_reset();native_mode=WIFI_MODE_STA; /* Do not overwrite an unverified external predecessor. */
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && !writes && !result.mutation_attempted);
    assert(!strcmp(s_radio.fault_stage,"mode-snapshot"));
    mode_reset();s_radio.effective_mode=native_mode=(wifi_mode_t)99;
    assert(SELECT(WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && !writes);
    mode_reset();s_radio.storage=native_storage=WIFI_STORAGE_FLASH;fail_at=3;
    assert(SELECT(WIFI_MODE_STA)==77 && result.rollback_complete && result.persistent_mutation_possible);
    assert(s_radio.fault_error==77 && native_mode==WIFI_MODE_NULL && !s_radio.cleanup_stage);
    assert(!locks && !critical && !helper_locks);return 0;
}
'''

VM_MAIN = r'''
static void mode_gc(void) {JS_GC(test_ctx);}
int main(int argc,char **argv) {
    assert(argc==4);unsigned total=0;bool expected=atoi(argv[2]);
    bool failed=!strcmp(argv[3],"sdk-error");
    int arity=!strcmp(argv[3],"arity-zero") ? 0 : !strcmp(argv[3],"arity-two") ? 2 : 1;
    for(unsigned nth=0;nth<=total;++nth) {
        uint8_t *heap=malloc(96*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"mode",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        mode_reset();mode_hook=mode_gc;if(failed)native_fail_at=2;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=js_wifi_driver_set_mode(ctx,NULL,arity,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity!=1 || (!expected && !failed))assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==77);
                assert(native_mode==WIFI_MODE_NULL && !s_radio.fault_stage);
            }
        } else {
            static const char *const names[]={"off","station","softAP","station+softAP"};
            JSCStringBuf buffer;const char *text=JS_ToCString(ctx,*output,&buffer);assert(text);
            assert(!strcmp(text,names[native_mode]) && !calls && s_radio.effective_mode==native_mode);
            assert(native_calls==(native_mode==WIFI_MODE_NULL ? 1U : 3U));
        }
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
