"""Production configure binding and error conversion with VM allocation/GC faults.

Capture/apply/status are explicit boundaries; their production implementations
have separate fixtures. This checks the public allocation/free contract and
per-call error metadata, not RF behavior or a duplicate native state machine.
"""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run
from test_wifi_config_controls import PRELUDE, sdk_types, structure

STATE = r'''
static struct {int sta_detach_error;} s_wifi_state;
static bool s_wifi_configuration_cleanup;
typedef struct {
    const char *fault_stage;bool restart_required;
    esp32_mquickjs_wifi_radio_config_result_t configuration,activation;
} esp32_mquickjs_wifi_radio_status_t;
static esp32_mquickjs_wifi_radio_status_t native_radio;
static int scenario,apply_calls,status_calls,diagnostic_calls,free_calls;
static bool driver_started;
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *out) {
    assert(!native_live);diagnostic_calls++;*out=native_radio;return ESP_OK;
}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) {return 0;}
static const char *esp_err_to_name(int error) {(void)error;return "injected-sdk-error";}
'''

BOUNDARIES = r'''
static bool esp32_mquickjs_wifi_capture_configuration(JSContext *ctx,JSValue options,
    esp32_mquickjs_wifi_configuration_t *capture) {
    assert(native_live==1 && JS_GetClassID(ctx,options)>=0);
    memset(capture,0x55,sizeof(*capture));memcpy(capture->station.sta.password,"capture-secret",14);
    if(scenario==6) {JS_ThrowTypeError(ctx,"injected capture rejection");return false;}
    if(scenario==7) {esp32_mquickjs_wifi_throw_configuration_error(ctx,ESP_ERR_NOT_SUPPORTED,NULL,NULL);return false;}
    if(scenario==8) {wifi_station_unsupported(ctx,"heDcmSet","wifi.configure");return false;}
    return true;
}
static int esp32_mquickjs_wifi_apply_configuration(esp32_mquickjs_wifi_configuration_t *capture,
    esp32_mquickjs_wifi_configuration_execution_t *execution) {
    assert(native_live==1 && !memcmp(capture->station.sta.password,"capture-secret",14));apply_calls++;
    *execution=(esp32_mquickjs_wifi_configuration_execution_t){.stage="admission"};
    if(scenario==1)return ESP_ERR_INVALID_STATE;
    execution->admitted=true;execution->stop_attempted=true;s_wifi_configuration_cleanup=true;
    execution->stage="configuration-stop";if(scenario==2)return -77;
    execution->configuration_attempted=true;execution->stage="configuration-commit";
    native_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){.stage="country-config",
        .mutation_attempted=true,.persistent_mutation_possible=true,.error=-77};
    if(scenario==3)return -77;
    native_radio.configuration.stage="complete";native_radio.configuration.error=0;
    execution->resume_attempted=true;execution->stage="configuration-resume";
    if(scenario==4) {
        native_radio.fault_stage="tx-power-readback";
        native_radio.activation=(esp32_mquickjs_wifi_radio_config_result_t){.stage="tx-power-readback",.error=-77,.mutation_attempted=true};
        return -77;
    }
    if(scenario==5) {native_radio.fault_stage="start";return -77;}
    driver_started=true;s_wifi_configuration_cleanup=false;execution->stage="complete";return ESP_OK;
}
static JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx) {
    assert(!native_live && free_calls==1 && driver_started);status_calls++;
    return JS_NewObject(ctx);
}
static void verified_free(void *p) {
    assert(native_live==1 && !free_calls);
    for(size_t i=0;i<sizeof(esp32_mquickjs_wifi_configuration_t);i++)assert(((unsigned char *)p)[i]==0);
    free_calls++;heap_caps_free(p);
}
#define esp32_mquickjs_memory_payload_free verified_free
'''

MAIN = r'''
#undef esp32_mquickjs_memory_payload_free
static bool text_is(JSContext *ctx,JSValue object,const char *key,const char *expected) {
    JSCStringBuf b;JSValue value=JS_GetPropertyStr(ctx,object,key);size_t length;
    const char *s=JS_ToCStringLen(ctx,&length,value,&b);
    return s && length==strlen(expected) && !memcmp(s,expected,length);
}
int main(void) {
    for(scenario=0;scenario<=8;scenario++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef input_ref,result_ref,details_ref;
            JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*details=JS_PushGCRef(ctx,&details_ref);
            *input=JS_NewObject(ctx);apply_calls=status_calls=diagnostic_calls=free_calls=0;driver_started=false;
            s_wifi_configuration_cleanup=false;
            native_radio=(esp32_mquickjs_wifi_radio_status_t){
                .configuration={.stage="old-config",.persistent_mutation_possible=true},
                .activation={.stage="old-activation",.mutation_attempted=true}};
            /* Wrong arity has no native allocation or dispatch. */
            assert(JS_IsException(js_wifi_configure(ctx,NULL,0,NULL)) && !native_live && !apply_calls);JS_GetException(ctx);
            calls=0;fail_at=nth;inject=true;collect=true;
            *result=js_wifi_configure(ctx,NULL,1,input);
            inject=false;collect=false;
            assert(!native_live && free_calls<=1 && apply_calls<=1 && status_calls<=1);
            if(!nth) {
                total=calls;
                if(scenario==0)assert(!JS_IsException(*result) && driver_started && apply_calls==1 && status_calls==1);
                else {
                    assert(JS_IsException(*result) && JS_HasException(ctx));*result=JS_GetException(ctx);
                    if(scenario!=6) {
                        assert(text_is(ctx,*result,"operation","wifi.configure"));
                        assert(text_is(ctx,*result,"code",scenario>=7?"WIFI_CONFIG_UNSUPPORTED":"WIFI_CONFIG_FAILED"));
                        *details=JS_GetPropertyStr(ctx,*result,"details");assert(JS_GetClassID(ctx,*details)>=0);
                        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"password")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"ssid")));
                        assert((!JS_IsNull(JS_GetPropertyStr(ctx,*details,"configuration")))==(scenario>=3 && scenario<=5));
                        assert((!JS_IsNull(JS_GetPropertyStr(ctx,*details,"activation")))==(scenario==4));
                        if(scenario>=7)assert(text_is(ctx,*details,"stage","capture"));
                        if(scenario==8)assert(text_is(ctx,*details,"option","heDcmSet"));
                    }
                    assert(!driver_started && !status_calls);
                }
                assert(free_calls==1 && diagnostic_calls==(scenario>=1 && scenario<=5));
                assert(apply_calls==(scenario<=5));
            } else {
                assert(JS_IsException(*result) && JS_HasException(ctx));JS_GetException(ctx);
                /* Result/error allocation cannot replay apply or implicit cleanup. */
                if(scenario==0 && apply_calls)assert(driver_started && status_calls==1);
                if(scenario>=2 && scenario<=5 && apply_calls)assert(s_wifi_configuration_cleanup);
            }
            JS_PopGCRef(ctx,&details_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
            JS_GC(ctx);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
'''

class WiFiPublicConfigure(unittest.TestCase):
    def test_public_allocation_release_and_current_error_records(self):
        radio_header=(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi_header=(ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_wifi.h').read_text()
        wifi=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        future=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c').read_text()
        structs='\n'.join(structure(radio_header,n) for n in ('esp32_mquickjs_wifi_radio_config_controls_t',
            'esp32_mquickjs_wifi_radio_start_controls_t','esp32_mquickjs_wifi_radio_config_result_t'))
        structs+='\n'.join(structure(wifi_header,n) for n in ('esp32_mquickjs_wifi_configuration_t',
            'esp32_mquickjs_wifi_configuration_execution_t'))
        body=PRELUDE+sdk_types('esp32c5/representative')+structs+STATE
        body+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        body+=extract((CORE/'esp32_mquickjs.c').read_text(),'esp32_mquickjs_throw_native_error')
        body+=extract(wifi,'wifi_make_configuration_status')+extract(wifi,'esp32_mquickjs_wifi_throw_configuration_error')
        body+=extract(future,'wifi_station_unsupported')+BOUNDARIES+extract(wifi,'js_wifi_configure')
        with tempfile.TemporaryDirectory() as tmp:
            binary=build(tmp,body,MAIN)
            run([str(binary)])
