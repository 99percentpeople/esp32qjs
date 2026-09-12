"""Production raw capture + ByteView/VM, with shared policy-parser boundaries."""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run

BOUNDARIES = r'''
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define CONFIG_SOC_WIFI_SUPPORT_5G 1
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED -2
typedef int esp_err_t,wifi_interface_t;
/* Storage layout is a parser boundary here; production SDK layout is checked
 * by the target build and the separate full Station/AP parser fixtures. */
typedef union {
    struct { uint8_t ssid[32],password[64];struct { bool required; } pmf_cfg; } sta;
    struct { uint8_t ssid[32],password[64],ssid_len,channel;uint16_t beacon_interval;
        struct { bool required; } pmf_cfg; } ap;
} wifi_config_t;
static int policy_calls,policy_failure,validator_error;
static bool force_required;
static bool native_requires_pmf(JSContext *ctx,JSValue options) {
    JSValue v=JS_GetPropertyStr(ctx,options,"pmf");
    if (force_required && !JS_IsUndefined(v)) {
        JSCStringBuf b;const char *s=JS_IsString(ctx,v) ? JS_ToCString(ctx,v,&b) : NULL;
        if(!s || strcmp(s,"required")){JS_ThrowTypeError(ctx,"injected policy conflict");return false;}
    }
    if(JS_IsUndefined(v))return force_required;
    JSCStringBuf b;const char *s=JS_IsString(ctx,v) ? JS_ToCString(ctx,v,&b) : NULL;
    if(!s || (strcmp(s,"optional") && strcmp(s,"required") && strcmp(s,"disabled"))){JS_ThrowTypeError(ctx,"injected policy invalid");return false;}
    return !strcmp(s,"required");
}
static bool esp32_mquickjs_wifi_parse_station_config_for_operation(JSContext *ctx,int argc,JSGCRef *args,
    wifi_config_t *out,uint32_t *timeout,const char *operation) {
    assert(!strcmp(operation,"wifi.configure")||!strcmp(operation,"wifi.driver.setInterfaceConfig"));
    assert(argc==2);policy_calls++;*timeout=1000;
    if(policy_failure){JS_ThrowTypeError(ctx,"injected policy rejection");return false;}
    out->sta.pmf_cfg.required=native_requires_pmf(ctx,args[1].val);
    if(JS_HasException(ctx))return false;
    memcpy(out->sta.ssid,"config",6);memcpy(out->sta.password,"native-secret",13);return true;
}
static bool esp32_mquickjs_wifi_parse_ap_config_for_operation(JSContext *ctx,JSValue options,wifi_config_t *out,const char *operation) {
    assert(!strcmp(operation,"wifi.configure")||!strcmp(operation,"wifi.driver.setInterfaceConfig"));
    policy_calls++;
    if(policy_failure){JS_ThrowTypeError(ctx,"injected policy rejection");return false;}
    out->ap.pmf_cfg.required=native_requires_pmf(ctx,options);
    if(JS_HasException(ctx))return false;
    memcpy(out->ap.ssid,"config",6);out->ap.ssid_len=6;
    memcpy(out->ap.password,"native-secret",13);out->ap.channel=1;out->ap.beacon_interval=100;return true;
}
static int esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config) {
    assert(config->ap.ssid_len>0 && config->ap.ssid_len<=32);return validator_error;
}
static JSValue esp32_mquickjs_wifi_throw_configuration_error(JSContext *ctx,int error,
    const char *option,const void *execution) {
    (void)error;(void)option;assert(!execution);
    return JS_ThrowTypeError(ctx,"injected native validation error");
}
static JSValue esp32_mquickjs_wifi_throw_operation_error(JSContext *ctx,const char *code,
    const char *operation,int error,int reason,uint32_t timeout) {
    (void)code;(void)operation;(void)error;(void)reason;(void)timeout;
    return JS_ThrowTypeError(ctx,"injected unsupported config");
}
static JSValue get_property_fault(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
static JSValue get_index_fault(JSContext *ctx,JSValue object,uint32_t index) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyUint32(ctx,*root,index);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr get_property_fault
#define JS_GetPropertyUint32 get_index_fault
'''

MAIN = r'''
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv) {
    assert(argc==6);int interface=atoi(argv[2]),expected=atoi(argv[3]);const char *mode=argv[5];
    size_t expected_length=strlen(argv[4])/2;uint8_t bytes[33]={0};
    for(size_t i=0;i<expected_length;i++){unsigned x;assert(sscanf(argv[4]+i*2,"%2x",&x)==1);bytes[i]=x;}
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef options_ref,view_ref;JSValue *options=JS_PushGCRef(ctx,&options_ref);
        *options=JS_Eval(ctx,argv[1],strlen(argv[1]),"config",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
        JSValue *view=JS_PushGCRef(ctx,&view_ref);
        if(strstr(mode,"view")) {
            size_t length=!strcmp(mode,"long-view")?33:expected_length;
            uint8_t *data=heap_caps_malloc(length?length:1,1);assert(data);memcpy(data,bytes,length);
            *view=esp32_mquickjs_new_owned_byte_view(ctx,data,length);assert(!JS_IsException(*view));
            assert(!JS_IsException(JS_SetPropertyStr(ctx,*options,"ssid",*view)));
            if(!strcmp(mode,"closed-view"))js_byte_view_close(ctx,view,0,NULL);
        }
        wifi_config_t config;memset(&config,0xa5,sizeof(config));
        calls=0;fail_at=nth;policy_calls=0;policy_failure=!strcmp(mode,"policy-fail");
        validator_error=!strcmp(mode,"validate-fail")?ESP_ERR_NOT_SUPPORTED:0;
        force_required=!strcmp(mode,"required");collect=1;inject=1;
        bool ok=!strcmp(mode,"driver") ? esp32_mquickjs_wifi_parse_driver_config_for_operation(ctx,*options,interface,&config,"wifi.driver.setInterfaceConfig")
            : esp32_mquickjs_wifi_parse_driver_config(ctx,*options,interface,&config);
        if(nth==0) {assert(ok==expected);total=calls;}
        if(ok) {
            uint8_t *ssid=interface==WIFI_IF_AP?config.ap.ssid:config.sta.ssid;
            assert(!memcmp(ssid,bytes,expected_length));
            for(size_t i=expected_length;i<32;i++)assert(ssid[i]==0);
            if(interface==WIFI_IF_AP) {
                assert(config.ap.ssid_len==expected_length);
                if(!strcmp(mode,"tu"))assert(config.ap.beacon_interval==60000 && config.ap.channel==36);
            }
        } else {
            assert(JS_HasException(ctx));JS_GetException(ctx);
            for(size_t i=0;i<sizeof(config);i++)assert(((uint8_t *)&config)[i]==0);
        }
        esp32_mquickjs_wireless_secure_zero(&config,sizeof(config));
        inject=0;collect=0;
        if(!JS_IsUndefined(*view))js_byte_view_close(ctx,view,0,NULL);
        JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&options_ref);assert(!root_count);
        JS_FreeContext(ctx);assert(!native_live);free(heap);
    }
    return 0;
}
'''


class WiFiDriverCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        radio = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        body = options + BOUNDARIES
        body += (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        body += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        body += extract(radio, 'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
        body += ''.join(extract(source, n) for n in ('wifi_capture_config_ssid', 'wifi_driver_config_unsupported',
            'esp32_mquickjs_wifi_parse_driver_config_for_operation', 'esp32_mquickjs_wifi_parse_driver_config'))
        cls.binary = build(cls.temp.name, body, MAIN)

    def capture(self, options, ap=False, expected=True, raw=b'A', mode=''):
        run([str(self.binary), '(' + options + ')', str(int(ap)), str(int(expected)), raw.hex(), mode])

    def test_text_arrays_arraylikes_and_tu(self):
        self.capture('{ssid:"A"}')
        self.capture('{ssid:[255,65]}', raw=bytes([255,65]))
        self.capture('{ssid:{length:2,0:255,1:65}}', raw=bytes([255,65]))
        self.capture('{ssid:[255,0,65],beaconIntervalTu:60000,channel:36}', ap=True, raw=bytes([255,0,65]), mode='tu')
        self.capture('{ssid:"A",password:"12345678",pmf:"optional"}', ap=True)
        self.capture('{ssid:"A",password:""}', ap=True)
        self.capture('{ssid:"A",pmf:"required"}', mode='required')
        self.capture('{ssid:"A",pmf:"disabled",disableWpa3CompatibleMode:true}')

    def test_public_driver_operation_uses_the_same_full_capture(self):
        self.capture('{ssid:"A",password:"12345678"}', mode='driver')
        self.capture('{ssid:[255,0,65]}', ap=True, raw=bytes([255,0,65]), mode='driver')
        self.capture('{ssid:"A",unknown:1}', expected=False, mode='driver')

    def test_byteview_read_lease_is_released_on_all_exits(self):
        self.capture('{}', raw=b'A', mode='view')
        self.capture('{}', ap=True, raw=bytes([255,0,65]), mode='view')
        self.capture('{}', expected=False, mode='closed-view')
        self.capture('{}', expected=False, mode='long-view')
        self.capture('{}', expected=False, raw=b'', mode='view')

    def test_binary_bounds_invalid_keys_and_failed_policy_clear_output(self):
        for options in ['{ssid:[]}', '{ssid:{length:4294967295}}', '{ssid:[0]}', '{ssid:[256]}',
                        '{ssid:[1.5]}', '{ssid:["65"]}', '{ssid:[1,undefined,2]}', '{ssid:"A",timeoutMs:10}',
                        '{ssid:"A",unexpected:true}', '{ssid:"A",pmf:1}']:
            self.capture(options, expected=False)
        for options in ['{ssid:"A",beaconIntervalTu:101}', '{ssid:"A",beaconIntervalMs:100}',
                        '{ssid:"A",channel:35}']:
            self.capture(options, ap=True, expected=False)
        self.capture('{ssid:"A",pmf:"optional"}', expected=False, mode='required')
        self.capture('{ssid:"A"}', expected=False, mode='policy-fail')
        self.capture('{ssid:"A"}', ap=True, expected=False, mode='validate-fail')
