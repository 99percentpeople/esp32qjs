"""Actual Station extension parser + MQuickJS; deferred Wi-Fi phase execution."""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run


SDK = r'''
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_OWE 9
#define WIFI_AUTH_WPA3_PSK 6
#define WIFI_AUTH_WPA2_WPA3_PSK 7
#define WIFI_AUTH_WAPI_PSK 8
#define WPA3_SAE_PWE_HUNT_AND_PECK 1
#define WPA3_SAE_PWE_HASH_TO_ELEMENT 2
#define WPA3_SAE_PWE_BOTH 3
#define WPA3_SAE_PK_MODE_AUTOMATIC 0
#define WPA3_SAE_PK_MODE_ONLY 1
#define WPA3_SAE_PK_MODE_DISABLED 2
typedef int wifi_sae_pwe_method_t;
typedef struct {
    uint16_t listen_interval;
    uint8_t failure_retry_cnt,channel,password[64],sae_h2e_identifier[32],ssid[32],bssid[6];
    int scan_method,sort_method,sae_pwe_h2e,sae_pk_mode;
    bool bssid_set,rm_enabled,btm_enabled,mbo_enabled,ft_enabled,owe_enabled;
    bool transition_disable,disable_wpa3_compatible_mode;
    uint32_t he_dcm_set:1,he_dcm_max_constellation_tx:2,he_dcm_max_constellation_rx:2;
    uint32_t he_mcs9_enabled:1,he_su_beamformee_disabled:1;
    uint32_t he_trig_su_bmforming_feedback_disabled:1,he_trig_mu_bmforming_partial_feedback_disabled:1;
    uint32_t he_trig_cqi_feedback_disabled:1,vht_su_beamformee_disabled:1,vht_mu_beamformee_disabled:1,vht_mcs8_enabled:1;
    struct { bool capable,required; } pmf_cfg;
    struct { int authmode;int8_t rssi;uint8_t rssi_5g_adjustment; } threshold;
} wifi_sta_config_t;
static int unsupported;
static const char *unsupported_option;
static bool wifi_station_unsupported(JSContext *ctx,const char *name,const char *operation) {
    assert(!strcmp(operation,"wifi.connect") || !strcmp(operation,"wifi.configure"));
    unsupported_option=name;unsupported++;
    JS_ThrowTypeError(ctx,"unsupported target option");return false;
}
/* Root the VM API's input before an injected moving collection. */
static JSValue get_property_with_fault(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr get_property_with_fault
'''

MAIN = r'''
#undef JS_GetPropertyStr
int main(int argc,char **argv) {
    assert(argc==4);int expected=atoi(argv[2]),adjustment=atoi(argv[3]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        wifi_sta_config_t sta={0};sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
        calls=0;unsupported=0;fail_at=nth;collect=1;inject=1;
        bool ok=wifi_parse_station_extensions(ctx,*root,&sta,"wifi.connect");
        if(nth==0) {
            assert(ok==expected);
            if(ok)assert(sta.threshold.rssi_5g_adjustment==adjustment);
            if(adjustment==-1)assert(unsupported==1);
            total=calls;
        }
        if(!ok){assert(JS_HasException(ctx));JS_GetException(ctx);}
        inject=0;collect=0;JS_PopGCRef(ctx,&r);assert(!root_count && !native_live);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
'''


class WiFiStationRssiPreference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        options = (CORE / 'esp32_mquickjs_options.c').read_text()
        helpers = '\n'.join(extract(options, name) for name in (
            'esp32_mquickjs_value_to_bounded_u32', 'esp32_mquickjs_value_to_enum'))
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c').read_text()
        body = '#include <math.h>\n' + helpers + SDK + ''.join(extract(source, name) for name in (
            'wifi_string_equals', 'wifi_parse_station_phy', 'wifi_parse_station_extensions'))
        cls.binaries = [build(cls.temp.name + '/' + str(gate),
            '#define CONFIG_SOC_WIFI_SUPPORT_5G ' + str(gate) + '\n' + body, MAIN) for gate in (0, 1)]

    def test_boundaries_and_property_gc_allocation_failures(self):
        for value in ('0', '1', '255'):
            run([str(self.binaries[1]), '({rssi5gAdjustment:' + value + '})', '1', value])
        for value in ('-1', '256', '1.5', 'NaN', 'Infinity', 'true', '"4"', 'null', '{}'):
            with self.subTest(value=value):
                run([str(self.binaries[1]), '({rssi5gAdjustment:' + value + '})', '0', '0'])

    def test_unavailable_target_rejects_even_explicit_zero(self):
        for value in ('0', '1', '255'):
            run([str(self.binaries[0]), '({rssi5gAdjustment:' + value + '})', '0', '-1'])
        for binary in self.binaries:
            run([str(binary), '({})', '1', '0'])
