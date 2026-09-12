"""Deferred production state readback, channel refresh overlap, decoding and VM GC."""
import re
import tempfile
import unittest
from test_wifi_config_controls import sdk_types, structure
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def state_code(profile, ap=True):
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n#include <assert.h>\n'
    code += f'#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n'
    code += sdk_types(profile, ('wifi_second_chan_t',))
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    for name in ('esp32_mquickjs_wifi_radio_driver_state_t', 'esp32_mquickjs_wifi_driver_state_query_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
    broker = (COMPONENT / 'internal/esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
    code += structure(broker, 'esp32_mquickjs_wifi_promiscuous_token_t')
    code += structure(radio, 'wifi_radio_live_lease_t')
    code += structure(header, 'esp32_mquickjs_wifi_driver_state_readback_t') + BOUNDARIES
    for name in ('wifi_radio_country_valid', 'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_refresh_channel',
                 'wifi_radio_get_channel_locked', 'esp32_mquickjs_wifi_radio_read_state'):
        code += extract(radio, name)
    return code + RESET


class WiFiDriverStateRead(unittest.TestCase):
    def test_real_native_state_readback_partial_errors_and_refresh_overlap(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, state_code(profile, ap) + MAIN)

    def test_public_arity_decoding_original_error_and_moving_gc(self):
        code = state_code('esp32c5/representative')
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code += extract(wifi, 'esp32_mquickjs_wifi_country_to_js')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_read_error', 'driver_state_to_js', 'driver_read_state',
                     'js_wifi_driver_get_mode', 'js_wifi_driver_get_country',
                     'js_wifi_driver_get_channel', 'js_wifi_driver_get_home_channel'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for query in range(4):
                for scenario in ('success', 'sdk-error', 'arity', 'decode'):
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
#define WIFI_RADIO_MAX_LEASES 2
static unsigned locks,critical,reads,current_reads,home_reads;
static int sdk_error;
static bool overlap;
static wifi_mode_t native_mode;
static wifi_country_t country;
static uint8_t current_channel,home_channel;
static wifi_second_chan_t second;
static struct {
    int lock;
    uint32_t generation,channel_generation;
    uint64_t channel_observation_revision;
    esp_err_t channel_observation_error;
    uint8_t primary_channel;
    wifi_second_chan_t secondary_channel;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    bool driver_owned,storage_configured,started,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
} s_radio;
static void wifi_radio_operation_lock(void) {assert(!locks && !critical);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1 && !critical);--locks;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(!critical);++critical;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(critical==1);--critical;} while(0)
static int state_sdk_step(void) {assert(locks==1 && !critical);++reads;return sdk_error;}
static int wifi_radio_refresh_channel(uint8_t *,wifi_second_chan_t *,uint32_t *);
static int esp_wifi_get_mode(wifi_mode_t *out) {*out=native_mode;return state_sdk_step();}
static int esp_wifi_get_country(wifi_country_t *out) {*out=country;return state_sdk_step();}
static int esp_wifi_get_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=current_channel;*secondary=second;++current_reads;
    if(overlap) {
        /* Interrupt the older SDK read with a later production refresh. */
        overlap=false;current_channel=11;uint8_t p;wifi_second_chan_t s;uint32_t g;
        assert(wifi_radio_refresh_channel(&p,&s,&g)==ESP_OK);
    }
    return state_sdk_step();
}
static int esp_wifi_get_home_channel(uint8_t *out,wifi_second_chan_t *secondary) {
    *out=home_channel;*secondary=second;++home_reads;return state_sdk_step();
}
'''

RESET = r'''
static void reset(void) {
    assert(!locks && !critical);memset(&s_radio,0,sizeof(s_radio));
    s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.generation=7;
    native_mode=WIFI_MODE_STA;current_channel=6;home_channel=1;second=WIFI_SECOND_CHAN_NONE;
    country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_AUTO};
    reads=current_reads=home_reads=0;sdk_error=0;overlap=false;
}
'''

MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_driver_state_readback_t out,zero={0};const char *stage;
#define READ(q) esp32_mquickjs_wifi_radio_read_state(ESP32_MQUICKJS_WIFI_DRIVER_STATE_##q,&out,&stage)
    for(unsigned q=0;q<4;++q) {
        reset();assert(esp32_mquickjs_wifi_radio_read_state(q,&out,&stage)==ESP_OK && !stage && reads==1);
        sdk_error=89;memset(&out,0xff,sizeof(out));
        assert(esp32_mquickjs_wifi_radio_read_state(q,&out,&stage)==89 && !memcmp(&out,&zero,sizeof(out)) && stage);
    }
    reset();overlap=true;assert(READ(CHANNEL)==ESP_OK && out.channel==11 && current_reads==2 && out.channel_generation==1);
    uint32_t generation=out.channel_generation;uint64_t revision=s_radio.channel_observation_revision;
    assert(READ(HOME_CHANNEL)==ESP_OK && out.channel==1 && !out.channel_generation && home_reads==1);
    assert(s_radio.primary_channel==11 && s_radio.channel_generation==generation && s_radio.channel_observation_revision==revision);
    reset();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(READ(MODE)==ESP_OK && READ(COUNTRY)==ESP_OK && READ(CHANNEL)==ESP_OK);
    unsigned before=reads;assert(READ(HOME_CHANNEL)==ESP_ERR_WIFI_NOT_STARTED && reads==before);
    s_radio.operation.identity=1;assert(READ(MODE)==ESP_ERR_INVALID_STATE && reads==before);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(READ(COUNTRY)==ESP_ERR_INVALID_STATE && reads==before);s_radio.lifecycle.identity=0;
    s_radio.restart_required=true;assert(READ(CHANNEL)==ESP_ERR_INVALID_STATE && reads==before);s_radio.restart_required=false;
    s_radio.driver_owned=false;assert(READ(MODE)==ESP_ERR_WIFI_NOT_INIT && reads==before);
    reset();native_mode=WIFI_MODE_NULL;assert(READ(MODE)==ESP_OK && out.mode==WIFI_MODE_NULL);
    native_mode=99;assert(READ(MODE)==ESP_ERR_INVALID_RESPONSE && !strcmp(stage,"decode"));
    native_mode=WIFI_MODE_AP;
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(READ(MODE)==ESP_OK);
#else
    assert(READ(MODE)==ESP_ERR_INVALID_RESPONSE);
#endif
    reset();current_channel=home_channel=36;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(READ(CHANNEL)==ESP_OK && READ(HOME_CHANNEL)==ESP_OK);
#else
    assert(READ(CHANNEL)==ESP_ERR_INVALID_RESPONSE && READ(HOME_CHANNEL)==ESP_ERR_INVALID_RESPONSE);
#endif
    reset();second=99;assert(READ(CHANNEL)==ESP_ERR_INVALID_RESPONSE && READ(HOME_CHANNEL)==ESP_ERR_INVALID_RESPONSE);
    reset();country.cc[0]=0;assert(READ(COUNTRY)==ESP_ERR_INVALID_RESPONSE);
    reset();country.policy=99;assert(READ(COUNTRY)==ESP_ERR_INVALID_RESPONSE);
    reset();country.schan=14;country.nchan=2;assert(READ(COUNTRY)==ESP_ERR_INVALID_RESPONSE);
    reset();country.cc[0]='0';country.cc[1]='1';country.cc[2]='X';assert(READ(COUNTRY)==ESP_OK);
    assert(!locks && !critical);return 0;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==3);unsigned query=atoi(argv[1]);bool failed=!strcmp(argv[2],"sdk-error"),arity=!strcmp(argv[2],"arity"),decode=!strcmp(argv[2],"decode");
    JSValue (*functions[])(JSContext *,JSValue *,int,JSValue *)={js_wifi_driver_get_mode,js_wifi_driver_get_country,js_wifi_driver_get_channel,js_wifi_driver_get_home_channel};
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,value_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*value=JS_PushGCRef(ctx,&value_ref);
        reset();sdk_error=failed ? 89 : 0;
        if(decode){native_mode=99;country.policy=99;second=99;}
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=functions[query](ctx,NULL,arity ? 1 : 0,value);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*result)==(failed || arity || decode));}
        if(arity)assert(!reads);
        if(JS_IsException(*result)) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && (failed || decode)) {
                *result=JS_GetPropertyStr(ctx,*result,"details");*value=JS_GetPropertyStr(ctx,*result,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*value) && n==(failed ? 89 : ESP_ERR_INVALID_RESPONSE));
                *value=JS_GetPropertyStr(ctx,*result,"interface");assert(JS_IsNull(*value));
            }
        } else if(query==0) {
            JSCStringBuf b;const char *text=JS_ToCString(ctx,*result,&b);assert(text && !strcmp(text,"station"));
        } else if(query==1) {
            *value=JS_GetPropertyStr(ctx,*result,"code");JSCStringBuf b;const char *text=JS_ToCString(ctx,*value,&b);assert(text && !strcmp(text,"US"));
        } else {
            *value=JS_GetPropertyStr(ctx,*result,"channel");int32_t n;assert(!JS_ToInt32(ctx,&n,*value) && n==(query==2 ? 6 : 1));
            *value=JS_GetPropertyStr(ctx,*result,"channelGeneration");
            if(query==3)assert(JS_IsNull(*value));else assert(!JS_ToInt32(ctx,&n,*value) && n==1);
        }
        JS_PopGCRef(ctx,&value_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical);free(heap);
    }
    return 0;
}
'''
