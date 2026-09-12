"""Deferred production BTM capture/encoder/Radio/supplicant boundary regressions.

Only SDK calls, scheduling and storage are injected. Do not run until the Wi-Fi
phase gate; compilation/execution is intentionally deferred with other fixtures.
"""
import re
import tempfile
import unittest
from test_wifi_vendor_ie import vendor_code, COMPONENT
from test_wifi_config_controls import structure, sdk_types
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run

MODULE = COMPONENT / 'src/modules/wifi_roaming'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_roaming.h'


def declarations():
    source = HEADER.read_text()
    return '\n'.join(re.findall(r'^#define ESP32_MQUICKJS_WIFI_BTM_.*$', source, re.M)) + '\n' + ''.join(
        structure(source, name) for name in ['esp32_mquickjs_wifi_btm_candidate_t',
            'esp32_mquickjs_wifi_btm_query_t', 'esp32_mquickjs_wifi_roaming_result_t'])


class WiFiRoaming(unittest.TestCase):
    def test_actual_radio_admission_and_sdk_dispatch(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = vendor_code('esp32c5/representative') + declarations()
        before = sdk_types('esp32c5/representative')
        with_ap = sdk_types('esp32c5/representative', ('wifi_ap_record_t',))
        assert with_ap.startswith(before)
        code += with_ap[len(before):]
        code += '\n#define CONFIG_ESP_WIFI_RRM_SUPPORT 1\n#define CONFIG_ESP_WIFI_WNM_SUPPORT 1\n#define ESP_FAIL -9\n#define ESP_ERR_WIFI_NOT_STARTED -10\n'
        code += SDK_BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        code += unit(MODULE / 'esp32_mquickjs_wifi_roaming_sdk.c')
        code += extract(radio, 'wifi_radio_connection_owner_locked')
        code += extract(radio, 'esp32_mquickjs_wifi_radio_roaming')
        compile_run(self, code + NATIVE_MAIN)

    def test_capture_and_pre_submit_gc_oom(self):
        code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_RRM_SUPPORT 1\n#define CONFIG_ESP_WIFI_WNM_SUPPORT 1\n#define CONFIG_ESP_WIFI_11R_SUPPORT 1\n#define CONFIG_IDF_TARGET "injected"\n'
        code += 'typedef int esp_err_t;\n#define ESP_OK 0\n' + declarations()
        code += extract((MODULE / 'esp32_mquickjs_wifi_roaming_sdk.c').read_text(), 'esp32_mquickjs_wifi_btm_encode')
        code += VM_BOUNDARY
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        options = (CORE / 'esp32_mquickjs_options.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace('#include "esp32_mquickjs_types.h"', '')
        code += options.replace('#include "esp32_mquickjs_options.h"', header)
        code += unit(MODULE / 'esp32_mquickjs_wifi_roaming.c')
        base = '{reason:"rssi",candidates:[{bssid:"02:00:00:00:00:01",bssidInformation:4294967295,operatingClass:81,channel:6,phyType:7,preference:0}]}'
        cases = [(base, True), ('{}', True), ('null', False),
                 (base.replace('"rssi"', '"rssi\\x00"'), False),
                 (base.replace('4294967295', '4294967296'), False),
                 (base.replace('channel:6', 'channel:6.5'), False),
                 (base.replace('02:00', '03:00'), False),
                 (base.replace('preference:0', 'preference:256'), False),
                 (base.replace('reason:', 'extra:1,reason:'), False),
                 (base.replace('bssid:', 'extra:1,bssid:'), False),
                 ('{candidates:new Array(17)}', False), ('{candidates:[undefined]}', False),
                 ('{allowApChannelChange:1}', False),
                 ('{get reason(){gc();return "delay";},get candidates(){gc();return [];}}', True),
                 ('{get reason(){throw 12345;}}', False)]
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in cases:
                run([str(binary), "(" + expression + ")", str(int(valid)), 'send'])
            for scenario in ['capabilities', 'rrm', 'btm', 'sdk-error']:
                run([str(binary), "(" + base + ")", '0' if scenario == 'sdk-error' else '1', scenario])


SDK_BOUNDARIES = r'''
static int dispatch_failure,query_code,read_error,link_error;
static unsigned dispatches,submissions;
static bool in_sdk,ap_supported=true,btm_enabled=true;
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {assert(in_sdk);memset(ap,0,sizeof(*ap));return link_error;}
static bool esp_rrm_is_rrm_supported_connection(void) { assert(in_sdk);return ap_supported; }
static bool esp_wnm_is_btm_supported_connection(void) { assert(in_sdk);return ap_supported; }
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(in_sdk && interface==WIFI_IF_STA);memset(config,0xa5,sizeof(*config));config->sta.btm_enabled=btm_enabled;return read_error;
}
static int esp_wnm_send_bss_transition_mgmt_query(int reason,const char *text,int cached) {
    assert(in_sdk && !critical && locks && !cached && reason>=0 && reason<=9);
    if(text)assert(strstr(text,",-1,81,6,7,030100"));
    ++submissions;return query_code;
}
static int eloop_register_timeout_blocking(int (*handler)(void *,void *),void *arg,void *unused) {
    assert(locks && !critical && !in_sdk);++dispatches;
    if(dispatch_failure)return -1; /* Includes eloop destruction before dispatch. */
    in_sdk=true;int r=handler(arg,unused);in_sdk=false;return r;
}
'''
NATIVE_MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_btm_query_t q={.reason=5,.count=1};
    q.candidates[0]=(esp32_mquickjs_wifi_btm_candidate_t){.bssid={2,0,0,0,0,1},.information=UINT32_MAX,
        .operating_class=81,.channel=6,.phy_type=7,.preference_set=true,.preference=0};
    char text[ESP32_MQUICKJS_WIFI_BTM_TEXT_BYTES];
    assert(esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    assert(!strcmp(text," neighbor=02:00:00:00:00:01,-1,81,6,7,030100"));
    q.candidates[0].information=0x80000000U;
    assert(esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)) && strstr(text,",-2147483648,"));
    q.candidates[0].information=UINT32_MAX;
    for(unsigned i=1;i<16;i++){q.candidates[i]=q.candidates[0];q.candidates[i].bssid[5]=i+1;}
    q.count=16;assert(esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    size_t length=strlen(text);assert(length<sizeof(text));
    assert(!esp32_mquickjs_wifi_btm_encode(&q,text,length) && !text[0]);
    q.count=17;assert(!esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    q.count=2;q.candidates[1]=q.candidates[0];assert(!esp32_mquickjs_wifi_btm_encode(&q,text,sizeof(text)));
    q.count=1;
    reset_vendor();s_radio.effective_mode=WIFI_MODE_STA;
    esp32_mquickjs_wifi_radio_lease_t app={0},sta={0},ap={0},foreign={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&app)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&sta)==ESP_OK);
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_roaming_result_t r;
#define EXEC(query) esp32_mquickjs_wifi_radio_roaming(&app,&sta,&ap,query,&r)
    assert(EXEC(NULL)==ESP_OK && r.rrm && r.btm && !r.submitted);
    assert(EXEC(&q)==ESP_OK && r.entered && r.submitted && r.radio_generation==7 && submissions==1);
    query_code=-1;assert(EXEC(&q)==ESP_FAIL && r.sdk_code==-1 && r.submitted);query_code=0;
    dispatch_failure=1;assert(EXEC(&q)==ESP_FAIL && !r.entered && !r.submitted);dispatch_failure=0;
    link_error=88;assert(EXEC(&q)==88 && !r.submitted && !strcmp(r.stage,"station-link"));link_error=0;
    btm_enabled=false;assert(EXEC(&q)==ESP_ERR_INVALID_STATE && !r.submitted);btm_enabled=true;
    ap_supported=false;assert(EXEC(NULL)==ESP_OK && !r.btm);assert(EXEC(&q)==ESP_ERR_NOT_SUPPORTED && !r.submitted);ap_supported=true;
    unsigned before=dispatches;
    s_radio.operation.identity=1;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);s_radio.lifecycle.identity=0;
    s_radio.cleanup_stage="injected";assert(EXEC(NULL)==ESP_ERR_INVALID_STATE);s_radio.cleanup_stage=NULL;
    ++sta.identity;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);--sta.identity;
    s_radio.leases[0].fixed_channel=true;assert(EXEC(&q)==ESP_ERR_INVALID_STATE);s_radio.leases[0].fixed_channel=false;
    assert(dispatches==before);
    s_radio.effective_mode=WIFI_MODE_APSTA;
    wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==ESP_OK);wifi_radio_operation_unlock();
    assert(EXEC(&q)==ESP_ERR_INVALID_STATE && !strcmp(r.stage,"ap-channel-permission"));
    q.allow_ap_channel_change=true;assert(EXEC(&q)==ESP_OK);
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE && !foreign.identity);
    wifi_radio_release_locked(&ap);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_OK);
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap)==ESP_OK);
    wifi_radio_operation_unlock();
    assert(EXEC(&q)==ESP_ERR_INVALID_STATE);
    assert(EXEC(NULL)==ESP_OK); /* Read query can coexist with unrelated owners. */
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    before=dispatches;assert(EXEC(NULL)==ESP_OK && !r.rrm && !r.btm && dispatches==before);
    assert(!locks && !critical && !in_sdk);
}
'''
VM_BOUNDARY = r'''
static unsigned submits;
static bool sdk_error;
static const char *esp_err_to_name(int err) {(void)err;return "injected";}
static esp_err_t esp32_mquickjs_wifi_roaming_execute(const esp32_mquickjs_wifi_btm_query_t *q,
    esp32_mquickjs_wifi_roaming_result_t *r) {
    *r=(esp32_mquickjs_wifi_roaming_result_t){.stage="complete",.entered=true,.rrm=true,.btm=true,.radio_generation=7};
    if(q){++submits;r->submitted=true;if(sdk_error){r->error=77;r->sdk_code=-55;}}
    return r->error;
}
'''
VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);int total=1;bool valid=atoi(argv[2]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"roaming",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        submits=0;sdk_error=!strcmp(argv[3],"sdk-error");
        bool caps=!strcmp(argv[3],"capabilities"),rrm=!strcmp(argv[3],"rrm"),btm=!strcmp(argv[3],"btm");
        calls=0;fail_at=nth;collect=inject=true;
        if(caps)*output=js_wifi_roaming_capabilities(ctx,NULL,0,NULL);
        else if(rrm)*output=js_wifi_roaming_is_rrm_supported(ctx,NULL,0,NULL);
        else if(btm)*output=js_wifi_roaming_is_btm_supported(ctx,NULL,0,NULL);
        else *output=js_wifi_roaming_send_btm_query(ctx,NULL,1,input);
        inject=collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!valid);}
        else assert(JS_IsException(*output));
        if(!caps && !rrm && !btm && !sdk_error)assert(submits==(unsigned)(!nth && valid));
        assert(submits<=1);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && sdk_error){*output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"sdkCode");int32_t value;assert(!JS_ToInt32(ctx,&value,*input) && value==-55);}
        } else if(rrm || btm)assert(*output==JS_TRUE);
        else if(!caps)assert(JS_GetPropertyStr(ctx,*output,"accepted")==JS_TRUE);
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);free(heap);
        assert(!root_count && !native_live);
    }
}
'''
