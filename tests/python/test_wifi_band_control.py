"""Deferred production band controls; no RF or live-AP switching claim."""
import re
import tempfile
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def band_code(profile):
    code = control_code(profile, True, ('wifi_band_t', 'wifi_second_chan_t', 'wifi_ap_record_t', 'wifi_phy_mode_t'))
    code = f'#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n' + code
    code = code.replace('esp32_mquickjs_wifi_radio_client_t client;} wifi_radio_live_lease_t;',
                        'esp32_mquickjs_wifi_radio_client_t client;bool fixed_channel,channel_conflict;uint32_t raw_tx_identity;} wifi_radio_live_lease_t;')
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    for name in ('esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_regulatory_channel',
                 'wifi_radio_band_snapshot', 'esp32_mquickjs_wifi_radio_change_band'):
        code += extract(radio, name)
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    return code + extract(wifi, 'esp32_mquickjs_wifi_apply_band') + RESET


class WiFiBandControl(unittest.TestCase):
    def test_real_band_admission_sdk_failures_readback_and_no_uncertain_replay(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, band_code(profile) + MAIN)

    def test_public_strict_names_fault_and_result_gc(self):
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = band_code('esp32c5/representative')
        code = re.sub(r'\bcalls\b', 'native_calls', code)
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_driver_query_t;', header).group(0)
        rate = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c').read_text()
        code += extract(rate, 'esp32_mquickjs_wifi_tx_phy_name') + unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_scalar_to_js', 'driver_phy_write_error', 'driver_change_band',
                     'js_wifi_driver_set_band', 'js_wifi_driver_set_band_mode'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for mode, expression, expected in [
                (0, '"5GHz"', True), (0, '"2.4GHz"', True),
                (1, '"2.4GHz-only"', True), (1, '"5GHz-only"', True), (1, '"auto"', True),
                (0, '"5GHz\\u0000"', False), (1, '"auto\\u0000"', False),
                (0, '5', False), (1, 'true', False), (1, '"5GHz"', False), (0, '"5ghz"', False),
            ]:
                run([str(binary), str(mode), expression, str(int(expected)), '0'])
            run([str(binary), '0', '"5GHz"', '0', '1'])


BOUNDARIES = r'''
#define ESP_ERR_WIFI_NOT_CONNECT 9
#define ESP_ERR_NOT_ALLOWED 10
static wifi_band_mode_t native_band_mode;
static wifi_band_t native_band;
static uint8_t native_channel;
static bool associated,corrupt_band,corrupt_channel,deny_regulatory;
static int esp_wifi_get_band_mode(wifi_band_mode_t *mode) {*mode=native_band_mode;return sdk_step(false);}
static int esp_wifi_get_band(wifi_band_t *band) {*band=corrupt_band ? 99 : native_band;return sdk_step(false);}
static int esp_wifi_set_band_mode(wifi_band_mode_t mode) {
    native_band_mode=mode;
    if(mode!=WIFI_BAND_MODE_AUTO) {native_band=mode==WIFI_BAND_MODE_2G_ONLY ? WIFI_BAND_2G : WIFI_BAND_5G;native_channel=native_band==WIFI_BAND_2G ? 1 : 36;}
    return sdk_step(true);
}
static int esp_wifi_set_band(wifi_band_t band) {native_band=band;native_channel=band==WIFI_BAND_2G ? 1 : 36;return sdk_step(true);}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {memset(ap,0,sizeof(*ap));int err=sdk_step(false);return err ? err : associated ? ESP_OK : ESP_ERR_WIFI_NOT_CONNECT;}
/* Existing callback-aware channel refresh has its own production fixture.
 * This is its native read boundary, not an alternate band transition model. */
static int wifi_radio_get_channel_locked(uint8_t *primary,wifi_second_chan_t *secondary,uint32_t *generation) {
    *primary=corrupt_channel ? 0 : native_channel;*secondary=WIFI_SECOND_CHAN_NONE;*generation=4;return sdk_step(false);
}
static int esp_wifi_get_country(wifi_country_t *country) {
    *country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.policy=WIFI_COUNTRY_POLICY_MANUAL};
#if CONFIG_SOC_WIFI_SUPPORT_5G
    country->wifi_5g_channel_mask=deny_regulatory ? 4 : 2; /* 40 vs 36 */
#endif
    return sdk_step(false);
}
'''

RESET = r'''
static void band_reset(void) {
    reset();memset(&s_ap_lease,0,sizeof(s_ap_lease));memset(&s_radio.leases[2],0,sizeof(s_radio.leases[2]));
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.storage=WIFI_STORAGE_RAM;
    native_band_mode=WIFI_BAND_MODE_2G_ONLY;native_band=WIFI_BAND_2G;native_channel=6;
    associated=corrupt_band=corrupt_channel=deny_regulatory=false;
}
'''

MAIN = r'''
int main(void) {
    int32_t actual;
    band_reset();associated=true;
    assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_OK && !writes && actual==WIFI_BAND_MODE_2G_ONLY);
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_2G,&actual,&result)==ESP_ERR_NOT_SUPPORTED && !writes);
#if CONFIG_SOC_WIFI_SUPPORT_5G
    band_reset();assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_AUTO,&actual,&result)==ESP_OK && actual==WIFI_BAND_MODE_AUTO);
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_OK && actual==WIFI_BAND_5G && native_channel==36);
    band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_OK);unsigned total=calls;
    for(unsigned nth=1;nth<=total;++nth) {
        band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;fail_at=nth;
        assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==77 && !actual);
        assert(!result.rollback_attempted && !locks && !critical && !helper_locks);
        if(result.mutation_attempted) {
            assert(s_radio.fault_error==77 && writes==1);
            unsigned before=calls;fail_at=0;assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_INVALID_STATE && calls==before);
        } else assert(!s_radio.fault_stage && !writes);
    }
    band_reset();associated=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_AUTO,&actual,&result)==ESP_ERR_INVALID_STATE && !writes);
    band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;corrupt_channel=true;
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_INVALID_RESPONSE && s_radio.fault_stage);
    band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;deny_regulatory=true;
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_NOT_ALLOWED && s_radio.fault_stage);
#else
    band_reset();assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_AUTO,&actual,&result)==ESP_ERR_NOT_SUPPORTED && !calls);
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    band_reset();s_radio.effective_mode=WIFI_MODE_APSTA;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();s_radio.leases[1].fixed_channel=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();s_wifi_state.radio_lease.generation++;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();s_wifi_state.scan_draining=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();corrupt_band=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_RESPONSE && !writes);
    return 0;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==5);bool mode=atoi(argv[1]),expected=atoi(argv[3]),sdk_fail=atoi(argv[4]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[2],strlen(argv[2]),"band",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;if(sdk_fail)native_fail_at=4;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=mode ? js_wifi_driver_set_band_mode(ctx,NULL,1,input) : js_wifi_driver_set_band(ctx,NULL,1,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(!expected && !sdk_fail)assert(!writes);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && sdk_fail)assert(s_radio.configuration.error==77 && s_radio.configuration.mutation_attempted && s_radio.fault_error==77);
        } else assert(JS_IsString(ctx,*output));
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
