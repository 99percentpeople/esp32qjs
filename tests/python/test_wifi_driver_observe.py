"""Deferred production Driver observations, SDK failures, public arity and VM GC."""
import json
import re
import tempfile
import unittest
from test_wifi_driver_phy import COMPONENT, ROOT, phy_types
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def observe_code(profile, ap=True):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {key.split('::')[-1]: item['declaration'] for key, item in symbols.items()}
    code = phy_types(profile, ap)
    code += f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(profile.startswith("esp32c5/"))}\n'
    for name in ('wifi_band_t', 'wifi_ps_type_t', 'wifi_phy_mode_t'):
        code += declarations[name] + ';\n'
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_driver_query_t;', header).group(0)
    rates = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c').read_text()
    code += extract(rates, 'esp32_mquickjs_wifi_tx_phy_name') + BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    return code + extract(radio, 'esp32_mquickjs_wifi_radio_read_driver')


class WiFiDriverObserve(unittest.TestCase):
    def test_native_sdk_error_decode_range_and_stable_state_admission(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, observe_code(profile, ap) + NATIVE_MAIN)

    def test_public_arity_sdk_error_details_scalar_values_gc_and_oom(self):
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        core = (CORE / 'esp32_mquickjs.c').read_text()
        code = observe_code('esp32c5/representative') + extract(core, 'esp32_mquickjs_throw_native_error')
        for name in ('tx_rate_interface', 'driver_read_error', 'driver_scalar_to_js', 'driver_read_scalar',
                     'js_wifi_driver_get_band', 'js_wifi_driver_get_band_mode', 'js_wifi_driver_get_power_save',
                     'js_wifi_driver_get_tx_power', 'js_wifi_driver_get_rssi', 'js_wifi_driver_get_aid',
                     'js_wifi_driver_get_negotiated_phy', 'js_wifi_driver_get_tsf_time', 'js_wifi_driver_get_inactive_time'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for query in range(9):
                for scenario in ('success', 'sdk-error', 'arity'):
                    run([str(binary), str(query), scenario])


BOUNDARIES = r'''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_WIFI_NOT_STARTED 5
#define ESP_ERR_INVALID_RESPONSE 6
static unsigned locks,reads;
static int sdk_error;
static wifi_band_t band=WIFI_BAND_2G;
static wifi_band_mode_t band_mode=WIFI_BAND_MODE_2G_ONLY;
static wifi_ps_type_t ps=WIFI_PS_MIN_MODEM;
static wifi_phy_mode_t phy=WIFI_PHY_MODE_HT40;
static int64_t tsf=INT64_C(9007199254740991);
static uint16_t aid=17,seconds=300;
static struct {
    bool driver_owned,storage_configured,started,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;
} s_radio={.driver_owned=true,.storage_configured=true,.started=true,
    .driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED,.effective_mode=WIFI_MODE_APSTA};
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
static int step_read(void) {assert(locks==1);++reads;return sdk_error;}
static int esp_wifi_get_band(wifi_band_t *out) {*out=band;return step_read();}
static int esp_wifi_get_band_mode(wifi_band_mode_t *out) {*out=band_mode;return step_read();}
static int esp_wifi_get_ps(wifi_ps_type_t *out) {*out=ps;return step_read();}
static int esp_wifi_get_max_tx_power(int8_t *out) {*out=73;return step_read();}
static int esp_wifi_sta_get_rssi(int *out) {*out=-63;return step_read();}
static int esp_wifi_sta_get_aid(uint16_t *out) {*out=aid;return step_read();}
static int esp_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *out) {*out=phy;return step_read();}
static int64_t esp_wifi_get_tsf_time(wifi_interface_t iface) {assert(iface==WIFI_IF_STA || iface==WIFI_IF_AP);step_read();return tsf;}
static int esp_wifi_get_inactive_time(wifi_interface_t iface,uint16_t *out) {assert(iface==WIFI_IF_STA || iface==WIFI_IF_AP);*out=seconds;return step_read();}
'''

NATIVE_MAIN = r'''
int main(void) {
    int64_t out;const char *stage;
    for(unsigned i=0;i<=ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME;++i) {
        out=55;assert(esp32_mquickjs_wifi_radio_read_driver(i,WIFI_IF_STA,&out,&stage)==ESP_OK && !stage && !locks);
        sdk_error=89;
        if(i!=ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME) {
            assert(esp32_mquickjs_wifi_radio_read_driver(i,WIFI_IF_STA,&out,&stage)==89 && out==0 && stage && !locks);
        }
        sdk_error=0;
    }
#define QUERY(kind) esp32_mquickjs_wifi_radio_read_driver(ESP32_MQUICKJS_WIFI_DRIVER_##kind,WIFI_IF_STA,&out,&stage)
    assert(QUERY(TX_POWER)==ESP_OK && out==73);
    assert(QUERY(RSSI)==ESP_OK && out==-63);
    aid=0;assert(QUERY(AID)==ESP_OK && out==0);
    tsf=0;assert(QUERY(TSF_TIME)==ESP_OK && out==0);
    tsf=INT64_C(9007199254740991);assert(QUERY(TSF_TIME)==ESP_OK && out==tsf);
    tsf++;assert(QUERY(TSF_TIME)==ESP_ERR_INVALID_RESPONSE && !out && !strcmp(stage,"decode"));
    tsf=-1;assert(QUERY(TSF_TIME)==ESP_ERR_INVALID_RESPONSE);tsf=0;
    band=99;assert(QUERY(BAND)==ESP_ERR_INVALID_RESPONSE);band=WIFI_BAND_5G;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(QUERY(BAND)==ESP_OK);
#else
    assert(QUERY(BAND)==ESP_ERR_INVALID_RESPONSE);
#endif
    band=WIFI_BAND_2G;band_mode=99;assert(QUERY(BAND_MODE)==ESP_ERR_INVALID_RESPONSE);band_mode=WIFI_BAND_MODE_2G_ONLY;
    ps=99;assert(QUERY(POWER_SAVE)==ESP_ERR_INVALID_RESPONSE);ps=WIFI_PS_NONE;
    phy=99;assert(QUERY(NEGOTIATED_PHY)==ESP_ERR_INVALID_RESPONSE);phy=WIFI_PHY_MODE_HT20;
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(QUERY(BAND)==ESP_OK && QUERY(AID)==ESP_OK && QUERY(TX_POWER)==ESP_ERR_WIFI_NOT_STARTED);
    assert(QUERY(TSF_TIME)==ESP_ERR_WIFI_NOT_STARTED && QUERY(INACTIVE_TIME)==ESP_ERR_WIFI_NOT_STARTED);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    unsigned before=reads;s_radio.driver_owned=false;assert(QUERY(BAND)==ESP_ERR_WIFI_NOT_INIT);s_radio.driver_owned=true;
    s_radio.operation.identity=1;assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.lifecycle.identity=0;
    s_radio.fault_stage="fault";assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.fault_stage=NULL;
    s_radio.cleanup_stage="cleanup";assert(QUERY(BAND)==ESP_ERR_INVALID_STATE);s_radio.cleanup_stage=NULL;
    s_radio.effective_mode=WIFI_MODE_AP;assert(QUERY(RSSI)==ESP_ERR_INVALID_STATE);
    assert(reads==before && !locks);
    int err=esp32_mquickjs_wifi_radio_read_driver(ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME,WIFI_IF_AP,&out,&stage);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(err==ESP_OK && out==300);
#else
    assert(err==ESP_ERR_NOT_SUPPORTED);
#endif
    return 0;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==3);unsigned query=atoi(argv[1]);int total=1;
    JSValue (*functions[])(JSContext *,JSValue *,int,JSValue *)={
        js_wifi_driver_get_band,js_wifi_driver_get_band_mode,js_wifi_driver_get_power_save,
        js_wifi_driver_get_tx_power,js_wifi_driver_get_rssi,js_wifi_driver_get_aid,
        js_wifi_driver_get_negotiated_phy,js_wifi_driver_get_tsf_time,js_wifi_driver_get_inactive_time};
    bool failed=!strcmp(argv[2],"sdk-error"),arity=!strcmp(argv[2],"arity");
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,input_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*input=JS_PushGCRef(ctx,&input_ref);
        *input=JS_NewString(ctx,"station");reads=0;calls=0;fail_at=nth;collect=true;inject=true;
        sdk_error=failed ? 89 : 0;tsf=failed ? -1 : INT64_C(9007199254740991);
        bool by_interface=query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME || query==ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME;
        *result=functions[query](ctx,NULL,arity ? 2 : by_interface ? 1 : 0,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*result)==(failed || arity));}
        if(arity)assert(reads==0);
        if(JS_IsException(*result)) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && failed) {
                *result=JS_GetPropertyStr(ctx,*result,"details");
                *input=JS_GetPropertyStr(ctx,*result,"espCode");int32_t n;assert(!JS_ToInt32(ctx,&n,*input));
                assert(n==(query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME ? ESP_ERR_INVALID_RESPONSE : 89));
                *input=JS_GetPropertyStr(ctx,*result,"interface");
                if(query<=ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER)assert(JS_IsNull(*input));
                else assert(JS_IsString(ctx,*input));
            }
        } else if(query==ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER || query==ESP32_MQUICKJS_WIFI_DRIVER_RSSI ||
                  query==ESP32_MQUICKJS_WIFI_DRIVER_AID || query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME ||
                  query==ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME) {
            double n;assert(!JS_ToNumber(ctx,&n,*result));
            assert(n==(query==ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER ? 18.25 : query==ESP32_MQUICKJS_WIFI_DRIVER_RSSI ? -63 :
                query==ESP32_MQUICKJS_WIFI_DRIVER_AID ? 17 : query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME ? 9007199254740991.0 : 300));
        } else assert(JS_IsString(ctx,*result));
        JS_PopGCRef(ctx,&input_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks);free(heap);
    }
    return 0;
}
'''
