"""Deferred production PMF mutation and preservation; injected SDK/VM only."""
import re
import tempfile
import unittest

from test_wifi_driver_storage import storage_code
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def pmf_code(profile, ap=True):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = storage_code(profile, ap) + BOUNDARIES
    code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
    for name in ('wifi_radio_config_equal', 'esp32_mquickjs_wifi_radio_pmf_disable_allowed',
                 'wifi_radio_restore_disabled_pmf', 'esp32_mquickjs_wifi_radio_disable_pmf'):
        code += extract(source, name)
    return code + RESET


class WiFiDriverPmf(unittest.TestCase):
    def test_production_security_admission_partial_writes_full_readback_and_restore(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, pmf_code(profile, ap) + MAIN)

    def test_public_strict_interface_arity_error_roots_and_no_success_allocation(self):
        code = re.sub(r'\bcalls\b', 'native_calls', pmf_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('tx_rate_interface', 'driver_phy_write_error', 'js_wifi_driver_disable_pmf'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('"station"', True), ('"access-point"', True),
                                      ('"station\\u0000extra"', False), ('"STATION"', False),
                                      ('"ap"', False), ('null', False), ('0', False), ('{}', False)]:
                run([str(binary), expression, str(int(valid)), 'normal'])
            for scenario in ('arity-zero', 'arity-two', 'sdk-error', 'security'):
                run([str(binary), '"station"', '0', scenario])


BOUNDARIES = r'''
static wifi_config_t configs[2];
static bool corrupt_pmf,corrupt_other;
static void (*pmf_hook)(void);
static int esp_wifi_get_config(wifi_interface_t iface,wifi_config_t *out) {
    *out=configs[iface];return sdk_step(false);
}
static int esp_wifi_disable_pmf_config(wifi_interface_t iface) {
    wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &configs[iface].sta.pmf_cfg : &configs[iface].ap.pmf_cfg;
    pmf->capable=false;int err=sdk_step(true); /* A prefix can change even when the SDK reports failure. */
    if(err==ESP_OK && !corrupt_pmf)pmf->required=false;
    if(corrupt_other)configs[iface].sta.ssid[0]^=1;
    if(pmf_hook)pmf_hook();return err;
}
'''

RESET = r'''
static void pmf_reset(void) {
    storage_reset();memset(configs,0,sizeof(configs));
    configs[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;configs[0].sta.disable_wpa3_compatible_mode=true;
    configs[1].ap.authmode=WIFI_AUTH_WPA2_PSK;
    configs[0].sta.pmf_cfg=(wifi_pmf_config_t){.capable=true,.required=true};
    configs[1].ap.pmf_cfg=configs[0].sta.pmf_cfg;
    memcpy(configs[0].sta.password,"station-secret",14);memcpy(configs[1].ap.password,"ap-secret",9);
    corrupt_pmf=corrupt_other=false;pmf_hook=NULL;
}
'''

MAIN = r'''
#define DISABLE(iface) esp32_mquickjs_wifi_radio_disable_pmf(iface,&result)
int main(void) {
    pmf_reset();assert(DISABLE(WIFI_IF_MAX)==ESP_ERR_INVALID_ARG && !calls);
    assert(esp32_mquickjs_wifi_radio_disable_pmf(WIFI_IF_STA,NULL)==ESP_ERR_INVALID_ARG && !calls);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(DISABLE(WIFI_IF_AP)==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    for(unsigned iface=0;iface<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2U : 1U);++iface) {
        for(unsigned failure=0;failure<6;++failure) {
            pmf_reset();wifi_config_t other=configs[1-iface];
            if(failure<=3)fail_at=failure;
            corrupt_pmf=failure==4;corrupt_other=failure==5;
            assert(DISABLE(iface)==(!failure ? ESP_OK : failure<=3 ? 77 : ESP_ERR_INVALID_RESPONSE));
            assert(!memcmp(&other,&configs[1-iface],sizeof(other)) && !s_radio.started && s_radio.generation==7);
            if(failure==1)assert(!writes && !result.mutation_attempted && !s_radio.fault_stage);
            else {
                assert(writes==1 && result.mutation_attempted && !result.rollback_attempted);
                if(failure) {
                    assert(s_radio.fault_stage && s_radio.fault_error==result.error);
                    unsigned before=calls;fail_at=0;assert(DISABLE(iface)==ESP_ERR_INVALID_STATE && calls==before);
                } else {
                    unsigned before=calls;assert(DISABLE(iface)==ESP_OK && calls==before+1 && writes==1 && !result.mutation_attempted);
                }
            }
        }
        pmf_reset();s_radio.storage=WIFI_STORAGE_FLASH;fail_at=2;
        assert(DISABLE(iface)==77 && result.persistent_mutation_possible && !result.rollback_attempted);
        for(int auth=-1;auth<=WIFI_AUTH_MAX;++auth) {
            pmf_reset();if(iface==0)configs[0].sta.threshold.authmode=auth;else configs[1].ap.authmode=auth;
            bool allowed=auth==WIFI_AUTH_OPEN || auth==WIFI_AUTH_WEP || auth==WIFI_AUTH_WPA_PSK ||
                auth==WIFI_AUTH_WPA2_PSK || auth==WIFI_AUTH_WPA_WPA2_PSK || auth==WIFI_AUTH_ENTERPRISE || auth==WIFI_AUTH_WPA_ENTERPRISE;
            assert(DISABLE(iface)==(allowed ? ESP_OK : ESP_ERR_NOT_SUPPORTED));
            if(!allowed)assert(calls==1 && !writes && !result.mutation_attempted && !strcmp(result.stage,"pmf-security"));
        }
        pmf_reset();if(iface==0)configs[0].sta.disable_wpa3_compatible_mode=false;else configs[1].ap.wpa3_compatible_mode=true;
        assert(DISABLE(iface)==ESP_ERR_NOT_SUPPORTED && !writes);
        /* A recorded disabled predecessor is re-applied only when the SDK
         * enabled PMF while restoring config. Never reinterpret new input. */
        pmf_reset();wifi_config_t prior=configs[iface];
        wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &prior.sta.pmf_cfg : &prior.ap.pmf_cfg;
        pmf->capable=pmf->required=false;
        wifi_radio_operation_lock();
        assert(wifi_radio_restore_disabled_pmf(iface,&prior)==ESP_OK && writes==1);
        unsigned before=calls;assert(wifi_radio_restore_disabled_pmf(iface,&prior)==ESP_OK && writes==1 && calls==before+1);
        wifi_radio_operation_unlock();
    }
    for(unsigned bad=0;bad<15;++bad) {
        pmf_reset();switch(bad) {
            case 0:s_radio.driver_owned=false;break;
            case 1:s_radio.storage_configured=false;break;
            case 2:s_radio.storage=99;break;
            case 3:s_radio.started=true;break;
            case 4:s_radio.stop_required=true;break;
            case 5:s_radio.restart_required=true;break;
            case 6:s_radio.lifecycle.identity=1;break;
            case 7:s_radio.operation.identity=1;break;
            case 8:s_radio.wake_locks=1;break;
            case 9:s_radio.promiscuous_claimed=true;break;
            case 10:s_tx_rate_lease.identity=1;break;
            case 11:s_tx_rate_lease.restore_pending=true;break;
            case 12:s_radio.fault_stage="other";break;
            case 13:s_radio.cleanup_stage="other";break;
            case 14:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;break;
        }
        assert(DISABLE(WIFI_IF_STA)==(bad ? ESP_ERR_INVALID_STATE : ESP_ERR_WIFI_NOT_INIT) && !calls);
    }
    for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i) {
        pmf_reset();s_radio.leases[i].identity=i+1;assert(DISABLE(WIFI_IF_STA)==ESP_ERR_INVALID_STATE && !calls);
    }
    assert(!locks && !critical && !helper_locks);return 0;
}
'''

VM_MAIN = r'''
static void pmf_gc(void) {JS_GC(test_ctx);}
int main(int argc,char **argv) {
    assert(argc==4);unsigned total=0;bool expected=atoi(argv[2]);
    bool failed=!strcmp(argv[3],"sdk-error"),security=!strcmp(argv[3],"security");
    int arity=!strcmp(argv[3],"arity-zero") ? 0 : !strcmp(argv[3],"arity-two") ? 2 : 1;
    for(unsigned nth=0;nth<=total;++nth) {
        void *heap=malloc(96*1024);JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"pmf",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        pmf_reset();pmf_hook=pmf_gc;if(failed)native_fail_at=2;if(security)configs[0].sta.threshold.authmode=WIFI_AUTH_WPA3_PSK;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=js_wifi_driver_disable_pmf(ctx,NULL,arity,input);inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity!=1 || (!expected && !failed && !security))assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && (failed || security)) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==(failed ? 77 : ESP_ERR_NOT_SUPPORTED));
                *input=JS_GetPropertyStr(ctx,*output,"interface");JSCStringBuf b;const char *name=JS_ToCString(ctx,*input,&b);assert(name && !strcmp(name,"station"));
            }
        } else assert(JS_IsUndefined(*output) && native_calls==3 && writes==1 && !calls);
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
