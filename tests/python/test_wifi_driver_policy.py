"""Deferred production SDK boolean policies; acceptance is not readback/recovery."""
import re
import tempfile
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def policy_code(profile, ap=True, coex=False):
    code = f'#define CONFIG_ESP_COEX_POWER_MANAGEMENT {int(coex)}\n' + control_code(profile, ap)
    code = code.replace('wifi_mode_t effective_mode;', 'wifi_mode_t effective_mode;bool stop_required;')
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_policy_control_t;', header).group(0)
    code += '\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_policy.h')
    code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c')
    code += 'static esp32_mquickjs_wifi_policy_state_t s_policies;\n'
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    return code + extract(radio, 'wifi_radio_policy_writer') + extract(radio, 'esp32_mquickjs_wifi_radio_policy_status') + extract(radio, 'esp32_mquickjs_wifi_radio_write_policy') + extract(wifi, 'esp32_mquickjs_wifi_apply_policy') + RESET


class WiFiDriverPolicy(unittest.TestCase):
    def test_native_state_owner_feature_gates_and_uncertain_failure(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                for coex in (False, True):
                    with self.subTest(profile=profile, ap=ap, coex=coex):
                        compile_run(self, policy_code(profile, ap, coex) + MAIN)

    def test_public_boolean_interface_arity_errors_and_moving_gc(self):
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for coex in (False, True):
            code = re.sub(r'\bcalls\b', 'native_calls', policy_code('esp32c5/representative', True, coex))
            code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
            code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
            for name in ('tx_rate_interface', 'driver_phy_write_error', 'driver_write_policy',
                         'js_wifi_driver_set_dynamic_carrier_sense', 'js_wifi_driver_configure_11b_rate',
                         'js_wifi_driver_set_coexistence_power_management'):
                code += extract(driver, name)
            with tempfile.TemporaryDirectory() as tmp:
                binary = build(tmp, code, VM_MAIN)
                for query in range(3):
                    for scenario in ('true', 'false', '1', '"true"', 'null', 'arity', 'sdk-error', 'interface'):
                        run([str(binary), str(query), scenario])


BOUNDARIES = r'''
static bool native_dynamic,native_11b[2],native_coex;
static int esp_wifi_set_dynamic_cs(bool value) {native_dynamic=value;return sdk_step(true);}
static int esp_wifi_config_11b_rate(wifi_interface_t iface,bool value) {assert(iface==WIFI_IF_STA || iface==WIFI_IF_AP);native_11b[iface]=value;return sdk_step(true);}
#if CONFIG_ESP_COEX_POWER_MANAGEMENT
static int esp_wifi_coex_pwr_configure(bool value) {native_coex=value;return sdk_step(true);}
#endif
'''

RESET = r'''
static void policy_reset(bool stopped) {
    reset();memset(&s_policies,0,sizeof(s_policies));native_dynamic=native_11b[0]=native_11b[1]=native_coex=false;
    if(stopped) {
        s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        memset(s_radio.leases,0,sizeof(s_radio.leases));s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;s_ap_lease.acquired=false;
    }
}
'''

MAIN = r'''
int main(void) {
    bool accepted;
#define WRITE(q,iface,v) esp32_mquickjs_wifi_apply_policy(ESP32_MQUICKJS_WIFI_POLICY_##q,iface,v,&accepted,&result)
    policy_reset(false);assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_OK && accepted && native_dynamic && calls==1);
    assert(s_policies.revision==1 && s_policies.records[0].known && s_policies.records[0].value);
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_OK && calls==2); /* No getter, no guessed deduplication. */
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,false)==ESP_OK && !accepted && !native_dynamic);
    policy_reset(true);assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_WIFI_NOT_STARTED && !calls);
    policy_reset(false);assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(true);assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_OK && native_11b[0] && !native_11b[1]);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(WRITE(11B_RATE,WIFI_IF_AP,true)==ESP_OK && native_11b[1]);
#else
    assert(WRITE(11B_RATE,WIFI_IF_AP,true)==ESP_ERR_NOT_SUPPORTED);
#endif
    policy_reset(true);s_radio.leases[3]=(wifi_radio_live_lease_t){23,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    for(unsigned q=0;q<3;++q) {
        policy_reset(q==1);fail_at=1;
        int err=esp32_mquickjs_wifi_apply_policy(q,q==1 ? WIFI_IF_STA : WIFI_IF_MAX,true,&accepted,&result);
#if !CONFIG_ESP_COEX_POWER_MANAGEMENT
        if(q==2){assert(err==ESP_ERR_NOT_SUPPORTED && !calls && !result.mutation_attempted);continue;}
#endif
        assert(s_policies.revision==1 && s_policies.records[q==2 ? 3 : q].uncertain && !s_policies.records[q==2 ? 3 : q].known);
        assert(err==77 && !accepted && writes==1 && s_radio.fault_error==77 && !result.rollback_attempted);
        assert(!locks && !critical && !helper_locks);
        fail_at=0;unsigned before=calls;
        assert(esp32_mquickjs_wifi_apply_policy(q,q==1 ? WIFI_IF_STA : WIFI_IF_MAX,true,&accepted,&result)==ESP_ERR_INVALID_STATE && calls==before);
    }
#if CONFIG_ESP_COEX_POWER_MANAGEMENT
    policy_reset(false);assert(WRITE(COEX_POWER,WIFI_IF_MAX,true)==ESP_OK && native_coex);
    policy_reset(true);assert(WRITE(COEX_POWER,WIFI_IF_MAX,true)==ESP_OK && native_coex);
#endif
    policy_reset(false);s_wifi_state.radio_lease.generation++;assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_radio.leases[3]=(wifi_radio_live_lease_t){23,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW};
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_radio.wake_locks=1;assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_wifi_state.scan_draining=true;assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(true);s_radio.stop_required=true;assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(true);s_radio.effective_mode=WIFI_MODE_NULL;assert(WRITE(11B_RATE,WIFI_IF_STA,true)==ESP_ERR_INVALID_STATE && !calls);
    policy_reset(false);s_policies.revision=UINT32_MAX;
    assert(WRITE(DYNAMIC_CS,WIFI_IF_MAX,true)==ESP_ERR_INVALID_STATE && !calls && !result.mutation_attempted && !s_radio.fault_stage);
    assert(!strcmp(result.stage,"policy-record-admission"));
    assert(!locks && !critical && !helper_locks);return 0;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==3);unsigned query=atoi(argv[1]);bool arity=!strcmp(argv[2],"arity"),failed=!strcmp(argv[2],"sdk-error"),bad_iface=!strcmp(argv[2],"interface");
    bool boolean=!strcmp(argv[2],"true") || !strcmp(argv[2],"false");
    bool unsupported=query==2 && !CONFIG_ESP_COEX_POWER_MANAGEMENT;
    bool success=boolean && !unsupported;
    JSValue (*functions[])(JSContext *,JSValue *,int,JSValue *)={js_wifi_driver_set_dynamic_carrier_sense,js_wifi_driver_configure_11b_rate,js_wifi_driver_set_coexistence_power_management};
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef arg0_ref,arg1_ref,output_ref;JSValue *a=JS_PushGCRef(ctx,&arg0_ref),*b=JS_PushGCRef(ctx,&arg1_ref),*output=JS_PushGCRef(ctx,&output_ref);
        const char *expr=arity || failed || bad_iface ? "true" : argv[2];
        *b=JS_Eval(ctx,expr,strlen(expr),"policy",JS_EVAL_RETVAL);assert(!JS_IsException(*b));
        *a=query==1 ? JS_NewString(ctx,bad_iface ? "station\0suffix" : "station") : *b;
        /* Embedded NUL needs an explicit length; ordinary C strings would hide it. */
        if(query==1 && bad_iface)*a=JS_NewStringLen(ctx,"station\0suffix",14);
        policy_reset(query==1);if(failed)native_fail_at=1;
        calls=0;fail_at=nth;collect=true;inject=true;
        JSValue args[2]={*a,*b};
        *output=functions[query](ctx,NULL,arity || (bad_iface && query!=1) ? 0 : query==1 ? 2 : 1,args);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!success);}
        if(arity || bad_iface || (!boolean && !failed) || unsupported)assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed && !unsupported) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*a=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*a) && n==77 && s_radio.fault_error==77);
                *a=JS_GetPropertyStr(ctx,*output,"interface");if(query!=1)assert(JS_IsNull(*a));
            }
        } else assert(JS_IsBool(*output) && (*output==JS_TRUE)==!strcmp(argv[2],"true"));
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&arg1_ref);JS_PopGCRef(ctx,&arg0_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
