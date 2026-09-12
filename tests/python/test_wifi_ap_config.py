"""Production AP capture/validator/readback/result; Wi-Fi phase run pending."""
import json
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run


BOUNDARIES = r'''
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_NOT_SUPPORTED -2
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000
#define ESP32_MQUICKJS_WIFI_AP_DTIM_MAX 10
typedef int esp_err_t;
typedef struct { wifi_ap_config_t ap; } wifi_config_t;
static int unsupported;
static JSValue esp32_mquickjs_wifi_throw_configuration_error(JSContext *ctx,int error,const char *option,const void *execution) {
    (void)error;(void)option;assert(!execution);return JS_ThrowTypeError(ctx,"unsupported configuration");
}
static JSValue esp32_mquickjs_wifi_throw_operation_error(JSContext *ctx,const char *code,
    const char *operation,int error,int reason,uint32_t status) {
    assert(!strcmp(code,"WIFI_AP_UNSUPPORTED") && !strcmp(operation,"wifi.startAP"));
    assert(error==ESP_ERR_NOT_SUPPORTED);(void)reason;(void)status;unsupported++;
    return JS_ThrowTypeError(ctx,"unsupported AP configuration");
}
static JSValue get_property_with_fault(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr get_property_with_fault
'''

MAIN = r'''
#undef JS_GetPropertyStr
static void check_readback(wifi_config_t *requested) {
    wifi_config_t actual=*requested;
    if(requested->ap.wpa3_compatible_mode)actual.ap.authmode=WIFI_AUTH_WPA2_PSK;
    assert(wifi_radio_ap_reopen_config_matches(requested,&actual));
    if(requested->ap.authmode!=WIFI_AUTH_OPEN && requested->ap.authmode!=WIFI_AUTH_WPA_PSK) {
        wifi_config_t changed=actual;changed.ap.pmf_cfg.capable=!requested->ap.pmf_cfg.capable;
        assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&changed));
        assert(!wifi_radio_ap_reopen_config_matches(requested,&changed));
        if(!requested->ap.pmf_cfg.capable || requested->ap.pmf_cfg.required) {
            changed=actual;changed.ap.pmf_cfg.required=!requested->ap.pmf_cfg.required;
            assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&changed));
        }
    }
#define CHECK_REOPEN_FIELD(field,value) do { wifi_config_t changed=actual;changed.ap.field=(value);assert(!wifi_radio_ap_reopen_config_matches(requested,&changed)); } while(0)
    CHECK_REOPEN_FIELD(channel,requested->ap.channel==1?6:1);
    CHECK_REOPEN_FIELD(ssid_hidden,!requested->ap.ssid_hidden);
    CHECK_REOPEN_FIELD(max_connection,requested->ap.max_connection==1?2:1);
    CHECK_REOPEN_FIELD(beacon_interval,requested->ap.beacon_interval+100);
    CHECK_REOPEN_FIELD(csa_count,requested->ap.csa_count==1?2:1);
    CHECK_REOPEN_FIELD(dtim_period,requested->ap.dtim_period==1?2:1);
    CHECK_REOPEN_FIELD(ftm_responder,!requested->ap.ftm_responder);
#undef CHECK_REOPEN_FIELD

    if(requested->ap.wpa3_compatible_mode)actual.ap.authmode=WIFI_AUTH_WPA2_PSK;
    assert(esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.sae_ext=!requested->ap.sae_ext;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.sae_ext=requested->ap.sae_ext;
    actual.ap.wpa3_compatible_mode=!requested->ap.wpa3_compatible_mode;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.wpa3_compatible_mode=requested->ap.wpa3_compatible_mode;
    actual.ap.bss_max_idle_cfg.period=requested->ap.bss_max_idle_cfg.period==10?11:10;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.bss_max_idle_cfg.period=requested->ap.bss_max_idle_cfg.period;
    actual.ap.bss_max_idle_cfg.protected_keep_alive=!requested->ap.bss_max_idle_cfg.protected_keep_alive;
    assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    actual.ap.bss_max_idle_cfg.protected_keep_alive=requested->ap.bss_max_idle_cfg.protected_keep_alive;
    if(requested->ap.wpa3_compatible_mode) {
        actual.ap.authmode=WIFI_AUTH_WPA3_PSK;
        assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
        actual.ap.authmode=WIFI_AUTH_WPA2_PSK;
        actual.ap.pairwise_cipher=WIFI_CIPHER_TYPE_TKIP;
        assert(!esp32_mquickjs_wifi_radio_accept_ap_config(requested,&actual));
    }
    esp32_mquickjs_wireless_secure_zero(&actual,sizeof(actual));
}
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]);const char *kind=argv[3];int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef r,result_ref,view_ref;JSValue *root=JS_PushGCRef(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        JSValue *view=JS_PushGCRef(ctx,&view_ref);
        if(!strcmp(kind,"binary-view") || !strcmp(kind,"closed-view")) {
            uint8_t *bytes=heap_caps_malloc(3,1);assert(bytes);bytes[0]=255;bytes[1]=0;bytes[2]=65;
            *view=esp32_mquickjs_new_owned_byte_view(ctx,bytes,3);assert(!JS_IsException(*view));
            assert(!JS_IsException(JS_SetPropertyStr(ctx,*root,"ssid",*view)));
            if(!strcmp(kind,"closed-view"))js_byte_view_close(ctx,view,0,NULL);
        }
        wifi_config_t config;memset(&config,0x55,sizeof(config));
        unsupported=0;calls=0;fail_at=nth;collect=1;inject=1;
        bool allow_disconnect=true;
        bool ok=esp32_mquickjs_wifi_parse_ap_config(ctx,*root,&config,&allow_disconnect);
        assert(allow_disconnect==(ok && !strcmp(kind,"allow-disconnect")));
        if(nth==0) {
            assert(ok==expected);
            if(!strcmp(kind,"unsupported"))assert(unsupported==1);
            if(ok) {
                check_readback(&config);
                if(!strcmp(kind,"ext"))assert(config.ap.sae_ext && config.ap.authmode==WIFI_AUTH_WPA3_PSK &&
                    config.ap.pairwise_cipher==WIFI_CIPHER_TYPE_GCMP256 && config.ap.pmf_cfg.required &&
                    config.ap.sae_pwe_h2e==WPA3_SAE_PWE_HASH_TO_ELEMENT);
                if(!strcmp(kind,"idle"))assert(config.ap.bss_max_idle_cfg.period==10 && config.ap.bss_max_idle_cfg.protected_keep_alive);
                if(!strcmp(kind,"pmf-disabled"))assert(!config.ap.pmf_cfg.capable && !config.ap.pmf_cfg.required);
                if(!strncmp(kind,"tu-",3))assert(config.ap.beacon_interval==(unsigned)atoi(kind+3));
                if(!strncmp(kind,"channel-",8))assert(config.ap.channel==(unsigned)atoi(kind+8));
                if(!strcmp(kind,"binary") || !strcmp(kind,"binary-view"))assert(config.ap.ssid_len==3 && config.ap.ssid[0]==255 && config.ap.ssid[1]==0 && config.ap.ssid[2]==65);
                if(!strcmp(kind,"binary32")) {assert(config.ap.ssid_len==32);for(size_t i=0;i<32;i++)assert(config.ap.ssid[i]==65);}
                if(!strcmp(kind,"nested"))assert(config.ap.max_connection==2 && config.ap.dtim_period==3 && !strcmp((char *)config.ap.password,"12345678"));
            }
        }
        if(!ok) {
            for(size_t i=0;i<sizeof(config);i++)assert(((uint8_t *)&config)[i]==0);
            assert(JS_HasException(ctx));JS_GetException(ctx);
        } else {
            /* Inject the native readback boundary's documented compatible base
             * values. This is not proof that the real driver performed it. */
            if(config.ap.wpa3_compatible_mode)config.ap.authmode=WIFI_AUTH_WPA2_PSK;
            JSValue *result=JS_PushGCRef(ctx,&result_ref);*result=wifi_ap_result(ctx,&config);
            if(JS_IsException(*result))JS_GetException(ctx);
            else {
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"password")));
                JSGCRef bytes_ref;JSValue *bytes=JS_PushGCRef(ctx,&bytes_ref);
                *bytes=JS_GetPropertyStr(ctx,*result,"ssidBytes");
                uint32_t length;assert(!JS_ToUint32(ctx,&length,JS_GetPropertyStr(ctx,*bytes,"length")) && length==config.ap.ssid_len);
                for(uint32_t i=0;i<length;i++) {uint32_t byte;assert(!JS_ToUint32(ctx,&byte,JS_GetPropertyUint32(ctx,*bytes,i)) && byte==config.ap.ssid[i]);}
                if(!strcmp(kind,"binary") || !strcmp(kind,"binary-view"))assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"ssid")));
                JS_PopGCRef(ctx,&bytes_ref);
                if(!strcmp(kind,"pmf-disabled")) {
                    *root=JS_GetPropertyStr(ctx,*result,"pmf");JSCStringBuf b;const char *s=JS_ToCString(ctx,*root,&b);
                    assert(s && !strcmp(s,"disabled"));
                }
                assert(JS_GetPropertyStr(ctx,*result,"saeExt")==JS_NewBool(config.ap.sae_ext));
                assert(JS_GetPropertyStr(ctx,*result,"wpa3CompatibleMode")==JS_NewBool(config.ap.wpa3_compatible_mode));
                assert(JS_GetPropertyStr(ctx,*result,"bssMaxIdleProtectedKeepAlive")==JS_NewBool(config.ap.bss_max_idle_cfg.protected_keep_alive));
                uint32_t period;
                assert(!JS_ToUint32(ctx,&period,JS_GetPropertyStr(ctx,*result,"bssMaxIdlePeriod")) && period==config.ap.bss_max_idle_cfg.period);
            }
            JS_PopGCRef(ctx,&result_ref);
        }
        if(nth==0)total=calls;
        esp32_mquickjs_wireless_secure_zero(&config,sizeof(config));
        inject=0;collect=0;
        if(!JS_IsUndefined(*view))assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&r);assert(!root_count && !native_live);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
'''


class WiFiAPConfig(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        inventory = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())
        symbols = inventory['variants']['esp32c5/representative']['symbols']
        # Use recorded public C declarations, including the actual bit widths.
        header = 'components/esp_wifi/include/esp_wifi_types_generic.h::'
        sdk = '\n'.join(symbols[header + n]['declaration'] + ';' for n in (
            'wifi_auth_mode_t', 'wifi_cipher_type_t', 'wifi_sae_pwe_method_t',
            'wifi_pmf_config_t', 'wifi_bss_max_idle_config_t', 'wifi_ap_config_t'))
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        radio = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        ap = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
        body = '#include "cutils.h"\n' + options + sdk + BOUNDARIES
        body += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        body += ''.join(extract(radio, n) for n in ('esp32_mquickjs_wifi_radio_5ghz_channel_bit',
            'esp32_mquickjs_wifi_radio_validate_ap_config', 'esp32_mquickjs_wifi_radio_accept_ap_config',
            'wifi_radio_ap_prestart_config_matches',
            'wifi_radio_ap_reopen_config_matches'))
        body += extract(ap, 'esp32_mquickjs_wifi_parse_ap_config_for_operation')
        body += (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        capture = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        body += ''.join(extract(capture, name) for name in ['wifi_capture_config_ssid', 'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties', 'esp32_mquickjs_wifi_parse_start_ap_config'])
        body += ''.join(extract(ap, n) for n in ('esp32_mquickjs_wifi_parse_ap_config', 'wifi_ap_auth_name',
            'wifi_ap_cipher_name', 'wifi_ap_pwe_name', 'wifi_ap_result'))
        cls.binaries = {}
        for profile, gcmp, compatible, idle, h2e in [('all', 1, 1, 1, 1), ('limited', 0, 0, 0, 1), ('no-h2e', 1, 0, 1, 0), ('five', 1, 1, 1, 1)]:
            gates = {'CONFIG_SOC_WIFI_SUPPORT_5G': int(profile == 'five'), 'CONFIG_ESP_WIFI_SOFTAP_SUPPORT': 1, 'CONFIG_ESP_WIFI_ENABLE_WPA3_SAE': 1,
                'CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT': 1, 'CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_SOFTAP': 1,
                'CONFIG_SOC_WIFI_GCMP_SUPPORT': gcmp, 'CONFIG_ESP_WIFI_GCMP_SUPPORT': gcmp,
                'CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT': compatible, 'CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT': idle,
                'CONFIG_ESP_WIFI_ENABLE_SAE_H2E': h2e, 'CONFIG_ESP_WIFI_FTM_ENABLE': 1,
                'CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT': 1}
            prefix = ''.join('#define ' + k + ' ' + str(v) + '\n' for k, v in gates.items())
            cls.binaries[profile] = build(cls.temp.name + '/' + profile, prefix + body, MAIN)

    def capture(self, options, expected=True, kind='', profile='all'):
        run([str(self.binaries[profile]), '(' + options + ')', str(int(expected)), kind])

    def test_existing_defaults_and_full_security_capture(self):
        for options in ['{ssid:"test"}', '{ssid:"test",password:"12345678"}',
                        '{ssid:"test",password:"12345678",authMode:"wpa2",pmf:"required",gtkRekeyIntervalSeconds:60,dtimPeriod:2,csaCount:4,beaconIntervalMs:120}',
                        '{ssid:"test",authMode:"owe"}']:
            self.capture(options)

    def test_disconnect_permission_is_boolean_top_level_and_committed_after_capture(self):
        self.capture('{ssid:"new-ap",allowDisconnect:true}', kind='allow-disconnect')
        self.capture('{ssid:"new-ap",allowDisconnect:false}')
        self.capture('{ssid:"new-ap",allowDisconnect:undefined}')
        for value in ('null', '1', '"true"', '{}', '[]'):
            self.capture('{ssid:"new-ap",allowDisconnect:' + value + '}', False)
        self.capture('{ssid:"new-ap",driver:{allowDisconnect:true}}', False)
        self.capture('{ssid:"new-ap",allowDisconnect:true,password:"short"}', False)

    def test_sae_ext_defaults_and_conflicts(self):
        self.capture('{ssid:"test",password:"x",saeExt:true}', kind='ext')
        for extra in [',authMode:"wpa2"', ',pmf:"optional"', ',pairwiseCipher:"ccmp"',
                      ',saePwe:"hunting-and-pecking"', ',wpa3CompatibleMode:true']:
            self.capture('{ssid:"test",password:"12345678",saeExt:true' + extra + '}', False)
        for profile in ['limited', 'no-h2e']:
            self.capture('{ssid:"test",password:"12345678",saeExt:true}', False, 'unsupported', profile)

    def test_explicit_pmf_disable_security_and_exact_readback(self):
        for auth in ('wpa2', 'wpa/wpa2'):
            self.capture('{ssid:"test",password:"12345678",authMode:"' + auth + '",pmf:"disabled"}', kind='pmf-disabled')
        for auth in ('wpa3', 'wpa2/wpa3', 'wpa'):
            self.capture('{ssid:"test",password:"12345678",authMode:"' + auth + '",pmf:"disabled"}', False)
        for options in ('{ssid:"test",pmf:"disabled"}', '{ssid:"test",authMode:"owe",pmf:"disabled"}',
                        '{ssid:"test",password:"12345678",wpa3CompatibleMode:true,pmf:"disabled"}',
                        '{ssid:"test",password:"12345678",pmf:"disabled\\u0000extra"}'):
            self.capture(options, False)

    def test_compatible_mode_and_canonical_readback(self):
        self.capture('{ssid:"test",password:"12345678",wpa3CompatibleMode:true,transitionDisable:true}')
        self.capture('{ssid:"test",password:"12345678",authMode:"wpa3",wpa3CompatibleMode:true}')
        for options in ['{ssid:"test",password:"short",wpa3CompatibleMode:true}',
                        '{ssid:"test",wpa3CompatibleMode:true}',
                        '{ssid:"test",password:"12345678",wpa3CompatibleMode:true,pairwiseCipher:"gcmp"}']:
            self.capture(options, False)
        self.capture('{ssid:"test",password:"12345678",wpa3CompatibleMode:true}', False, 'unsupported', 'limited')

    def test_idle_units_limits_security_and_gate(self):
        self.capture('{ssid:"test",password:"12345678",bssMaxIdlePeriod:10,bssMaxIdleProtectedKeepAlive:true}', kind='idle')
        for value in [0, 10, 65535]:
            self.capture('{ssid:"test",bssMaxIdlePeriod:' + str(value) + '}')
        for value in ['1', '9', '65536', '10.5', 'NaN', 'Infinity', 'true', '"10"']:
            self.capture('{ssid:"test",bssMaxIdlePeriod:' + value + '}', False)
        self.capture('{ssid:"test",bssMaxIdlePeriod:10,bssMaxIdleProtectedKeepAlive:true}', False)
        self.capture('{ssid:"test",password:"12345678",bssMaxIdleProtectedKeepAlive:true}', False)
        self.capture('{ssid:"test",bssMaxIdlePeriod:10}', False, 'unsupported', 'limited')
        self.capture('{ssid:"test",saeExt:false,wpa3CompatibleMode:false,bssMaxIdlePeriod:0,bssMaxIdleProtectedKeepAlive:false}', profile='limited')

    def test_bad_types_unknown_fields_and_secrets_are_cleared(self):
        for key in ['saeExt', 'wpa3CompatibleMode', 'bssMaxIdleProtectedKeepAlive']:
            for value in ['1', 'null', '"true"']:
                self.capture('{ssid:"test",password:"12345678",' + key + ':' + value + '}', False)
        for options in ['{}', '{ssid:""}', '{ssid:"a\\u0000b"}', '{ssid:"test",password:"abc\\u0000defgh"}',
                        '{ssid:"test",authMode:"wpa2"}',
                        '{ssid:"test",password:"12345678",authMode:"open"}']:
            self.capture(options, False)

    def test_nested_driver_merge_units_and_channels(self):
        self.capture('{ssid:"test",password:"12345678",maxConnections:2,driver:{dtimPeriod:3}}', kind='nested')
        self.capture('{ssid:"test",dtimPeriod:3,driver:{password:"12345678",maxConnections:2}}', kind='nested')
        for tu in (100, 300, 3100, 60000):
            self.capture('{ssid:"test",driver:{beaconIntervalTu:' + str(tu) + '}}', kind='tu-' + str(tu))
        for channel in (0, 1, 13, 14):
            self.capture('{ssid:"test",driver:{channel:' + str(channel) + '}}', kind='channel-' + str(channel))
        self.capture('{ssid:"test",driver:{channel:36}}', kind='channel-36', profile='five')
        self.capture('{ssid:"test",driver:{channel:36}}', False, 'unsupported')
        self.capture('{ssid:"test",driver:{password:""}}')
        self.capture('{ssid:"test",password:""}', False)
        self.capture('{ssid:"test",driver:{authMode:"wpa2",password:""}}', False)
        self.capture('{ssid:"test",password:"12345678",driver:{pmf:"disabled"}}', kind='pmf-disabled')
        self.capture('{ssid:"test",password:"x",driver:{saeExt:true}}', kind='ext')
        self.capture('{ssid:"test",password:"x",driver:{saeExt:true}}', False, 'unsupported', 'limited')

    def test_nested_driver_duplicates_bad_values_and_cross_layer_security(self):
        for options in ['{ssid:"test",driver:null}', '{ssid:"test",driver:[]}',
                        '{ssid:"test",driver:{ssid:"other"}}', '{ssid:"test",driver:{beaconIntervalMs:100}}',
                        '{ssid:"test",driver:{unknown:1}}', '{ssid:"test",driver:{driver:{}}}',
                        '{ssid:"test",driver:{"pmf\\u0000suffix":"required"}}',
                        '{ssid:"test",beaconIntervalMs:102.4,driver:{beaconIntervalTu:100}}',
                        '{ssid:"test",channel:1,driver:{channel:1}}',
                        '{ssid:"test",password:"12345678",driver:{password:"12345678"}}',
                        '{ssid:"test",password:"12345678",driver:{authMode:"open"}}',
                        '{ssid:"test",pmf:"optional",driver:{authMode:"wpa3",password:"x"}}']:
            self.capture(options, False)
        for value in ('0', '99', '101', '60001', '65536', '100.5', 'NaN', 'Infinity', '"100"', 'true'):
            self.capture('{ssid:"test",driver:{beaconIntervalTu:' + value + '}}', False)
        for value in ('-1', '15', '35', '178', '256', '1.5', 'NaN', '"1"', 'true'):
            self.capture('{ssid:"test",driver:{channel:' + value + '}}', False)
        self.capture('{ssid:"test",driver:{}}')
        self.capture('{ssid:"test",driver:undefined}')
        self.capture('{ssid:"test",channel:undefined,driver:{channel:13}}', kind='channel-13')
        self.capture('{ssid:"test",channel:1,driver:{channel:undefined}}', kind='channel-1')

    def test_binary_ssid_input_and_exact_start_result(self):
        self.capture('{}', kind='binary-view')
        self.capture('{}', False, kind='closed-view')
        self.capture('{ssid:[255,0,65]}', kind='binary')
        self.capture('{ssid:{length:3,0:255,1:0,2:65},driver:{beaconIntervalTu:300}}', kind='binary')
        self.capture('{ssid:[' + ','.join(['65']*32) + ']}', kind='binary32')
        for source in ('[]', '[256]', '[NaN]', '[1.5]', '[' + ','.join(['65']*33) + ']'):
            self.capture('{ssid:' + source + '}', False)
