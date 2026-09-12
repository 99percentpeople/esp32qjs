"""Deferred real RSSI write history/status conversion; no armed/RF simulation.

The production owner gate, setter and status getter are used with SDK/native
storage boundaries. Physical-generation changes below are isolated state inputs,
not evidence of actual STOP/deinit/init or event delivery.
"""
import re
import tempfile
import unittest

from test_wifi_connection_controls import control_code
from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import build, extract, run


class WiFiRssiRequest(unittest.TestCase):
    def test_actual_request_revision_error_history_and_generation(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, control_code(profile, ap) + NATIVE_MAIN)

    def test_real_configuration_checkpoint_replay_never_rearms_rssi(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                code = 'static unsigned rssi_calls;\n' + config_code(profile, True, mutation_boundary=True)
                code = code.replace('threshold=value;return sdk_step(true);',
                                    '++rssi_calls;threshold=value;return sdk_step(true);')
                code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
                compile_run(self, code + RESTART_MAIN)


class WiFiRssiRequestStatus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        code = control_code('esp32c5/representative')
        code = re.sub(r'\bcalls\b', 'native_calls', code)
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += 'static const char *esp_err_to_name(int error) { return error==ESP_OK ? "ESP_OK" : "SDK_ERROR"; }\n'
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code += extract(driver, 'esp32_mquickjs_wifi_rssi_request_to_js')
        cls.binary = build(cls.temp.name, code, VM_MAIN)

    def test_real_converter_nullable_history_failure_and_every_vm_allocation(self):
        run([str(self.binary)])


NATIVE_MAIN = r'''
static int request(int32_t dbm) {
    int32_t actual;
    return esp32_mquickjs_wifi_apply_connection_control(
        ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,dbm,&actual,&result);
}
int main(void) {
    esp32_mquickjs_wifi_rssi_request_t state;
    reset();assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!state.revision && !calls && !writes);
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(NULL) && !locks);
    assert(request(-75)==ESP_OK && calls==1 && writes==1);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(state.generation==7 && state.revision==1 && state.requested_dbm==-75 && state.error==ESP_OK);
    assert(request(-75)==ESP_OK && calls==2 && writes==2);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state) && state.revision==2);
    fail_at=3;assert(request(-80)==77 && threshold==-80 && writes==3);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(state.revision==3 && state.requested_dbm==-80 && state.error==77 && !s_radio.fault_stage);
    esp32_mquickjs_wifi_rssi_request_t saved=state;
    unsigned previous_calls=calls;
    s_wifi_state.scan_draining=true;
    assert(request(-60)==ESP_ERR_INVALID_STATE && !result.mutation_attempted);
    s_wifi_state.scan_draining=false;
    assert(request(-101)==ESP_ERR_INVALID_ARG && !result.mutation_attempted);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!memcmp(&saved,&state,sizeof(state)) && calls==previous_calls);
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state)); /* Same generation, no armed claim. */
    s_radio.driver_owned=false;
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!memcmp(&saved,&state,sizeof(state)) && calls==previous_calls);
    s_radio.driver_owned=true;++s_radio.generation;
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!memcmp(&saved,&state,sizeof(state)) && calls==previous_calls);
    /* Recreate the injected helper generation; only an explicit request writes. */
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_wifi_application.generation=s_wifi_state.radio_lease.generation=s_ap_lease.generation=s_radio.generation;
    fail_at=0;assert(request(-60)==ESP_OK && calls==previous_calls+1);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(state.generation==8 && state.revision==4 && state.requested_dbm==-60 && !state.error);
    s_rssi_request.revision=UINT32_MAX-1;
    assert(request(10)==ESP_OK && s_rssi_request.revision==UINT32_MAX);
    saved=s_rssi_request;previous_calls=calls;
    assert(request(-100)==ESP_ERR_NO_MEM && !result.mutation_attempted);
    assert(!strcmp(result.stage,"rssi-threshold-identity") && !s_radio.fault_stage);
    assert(calls==previous_calls && !memcmp(&saved,&s_rssi_request,sizeof(saved)));
    ++s_radio.generation;
    s_wifi_application.generation=s_wifi_state.radio_lease.generation=s_ap_lease.generation=s_radio.generation;
    assert(request(-70)==ESP_ERR_NO_MEM && calls==previous_calls);
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state) && state.revision==UINT32_MAX);
    assert(!locks && !critical && !helper_locks);
    return 0;
}
'''


RESTART_MAIN = r'''
int main(void) {
    for(unsigned failure=0;failure<2;++failure) {
        setup();rssi_calls=0;
        /* Establish an explicit request through the real stopped owner gate.
         * State transitions are SDK fixture inputs, not physical STOP proof. */
        s_radio.lifecycle.identity=0;s_radio.started=s_radio.stop_required=false;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        int32_t actual;fail_at=failure ? 1 : 0;
        assert(esp32_mquickjs_wifi_radio_connection_control(NULL,NULL,NULL,
            ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result)==(failure ? 77 : ESP_OK));
        assert(rssi_calls==1);esp32_mquickjs_wifi_rssi_request_t saved=s_rssi_request;
        s_radio.lifecycle.identity=41;s_radio.started=s_radio.stop_required=native_running=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;calls=writes=fail_at=0;
        wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        new_driver();threshold=12345; /* Arbitrary boundary sentinel, not an SDK default. */
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
        s_radio.started=s_radio.stop_required=native_running=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
        esp32_mquickjs_wifi_rssi_request_t observed;
        assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&observed));
        assert(!memcmp(&observed,&saved,sizeof(saved)) && rssi_calls==1 && threshold==12345);
    }
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
'''


VM_MAIN = r'''
static void number(JSContext *ctx,JSValue *root,const char *key,int32_t expected) {
    JSValue value=JS_GetPropertyStr(ctx,*root,key);int32_t actual;
    assert(!JS_IsException(value) && !JS_ToInt32(ctx,&actual,value) && actual==expected);
}
static void boolean(JSContext *ctx,JSValue *root,const char *key,bool expected) {
    assert(JS_GetPropertyStr(ctx,*root,key)==(expected ? JS_TRUE : JS_FALSE));
}
int main(void) {
    for(unsigned scenario=0;scenario<5;++scenario) {
        int total=1;
        for(int nth=0;nth<=total;++nth) {
            void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
            JSGCRef root_ref;JSValue *root=JS_PushGCRef(ctx,&root_ref);
            reset();int32_t actual;
            if(scenario) {
                if(scenario==2)native_fail_at=1;
                int err=esp32_mquickjs_wifi_apply_connection_control(
                    ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result);
                assert(err==(scenario==2 ? 77 : ESP_OK));
            }
            if(scenario==3)s_radio.driver_owned=false;
            if(scenario==4){++s_radio.generation;s_rssi_request.revision=UINT32_MAX;}
            esp32_mquickjs_wifi_rssi_request_t saved=s_rssi_request;
            unsigned before=native_calls;
            calls=0;fail_at=nth;collect=true;inject=true;
            *root=esp32_mquickjs_wifi_rssi_request_to_js(ctx);
            inject=false;collect=false;
            if(!nth){total=calls;assert(!JS_IsException(*root));}
            if(JS_IsException(*root)) {
                assert(JS_HasException(ctx));JS_GetException(ctx);
            } else {
                boolean(ctx,root,"accepted",scenario!=0 && scenario!=2);
                boolean(ctx,root,"generationActive",scenario==1 || scenario==2);
                boolean(ctx,root,"identityExhausted",scenario==4);
                if(!scenario) {
                    number(ctx,root,"revision",0);
                    assert(JS_GetPropertyStr(ctx,*root,"generation")==JS_NULL);
                    assert(JS_GetPropertyStr(ctx,*root,"requestedDbm")==JS_NULL);
                    assert(JS_GetPropertyStr(ctx,*root,"espCode")==JS_NULL);
                    assert(JS_GetPropertyStr(ctx,*root,"espName")==JS_NULL);
                } else {
                    number(ctx,root,"generation",7);number(ctx,root,"requestedDbm",-75);
                    number(ctx,root,"espCode",scenario==2 ? 77 : ESP_OK);
                }
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*root,"armed")));
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*root,"value")));
            }
            assert(native_calls==before && !memcmp(&saved,&s_rssi_request,sizeof(saved)));
            JS_PopGCRef(ctx,&root_ref);JS_FreeContext(ctx);
            assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
        }
    }
    return 0;
}
'''
