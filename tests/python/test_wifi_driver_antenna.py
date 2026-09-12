"""Deferred production PHY observations: admission, partial reads and VM GC/OOM.

SDK storage and lock boundaries are injected; no RF or GPIO routing is simulated.
These fixtures are written for the collective Wi-Fi stage, not executed yet.
"""
import re
import tempfile
import unittest

from test_wifi_config_controls import sdk_types
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def antenna_code(profile):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n'
    code += sdk_types(profile, ('esp_phy_ant_config_t', 'esp_phy_ant_gpio_config_t'))
    for kind, name in (('enum', 'esp32_mquickjs_wifi_radio_driver_state_t'),
                       ('union', 'esp32_mquickjs_wifi_antenna_snapshot_t')):
        code += re.search(r'typedef ' + kind + r' \{[^}]*\} ' + name + ';', header).group(0)
    return code + BOUNDARIES + extract(radio, 'esp32_mquickjs_wifi_radio_read_antenna') + RESET


class WiFiDriverAntenna(unittest.TestCase):
    def test_production_admission_partial_reads_full_width_and_decode(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram',
                        'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, antenna_code(profile) + MAIN)

    def test_public_fields_errors_arity_and_each_vm_allocation(self):
        code = antenna_code('esp32c5/representative')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_read_error', 'driver_antenna_to_js', 'driver_read_antenna',
                     'js_wifi_driver_get_antenna', 'js_wifi_driver_get_antenna_gpio'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for query in range(2):
                for scenario in ('success', 'sdk-error', 'arity', 'admission', 'decode'):
                    if query == 1 and scenario == 'decode':
                        continue  # All GPIO bit-field representations are retained.
                    run([str(binary), str(query), scenario])


BOUNDARIES = r'''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_INVALID_RESPONSE 6
static unsigned locks,reads,ant_reads,gpio_reads;
static int sdk_error;
static struct {
    bool driver_owned,storage_configured,restart_required;
    struct {unsigned identity;} lifecycle,operation;
    const char *fault_stage,*cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    /* Observations must not mutate unrelated owners, wake or routing state. */
    unsigned lease_identity,wake_locks;
    bool promiscuous_claimed;
} s_radio;
static esp_phy_ant_config_t stored_ant;
static esp_phy_ant_gpio_config_t stored_gpio;
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
static int esp_phy_get_ant(esp_phy_ant_config_t *out) {
    assert(locks==1);++reads;++ant_reads;*out=stored_ant;return sdk_error;
}
static int esp_phy_get_ant_gpio(esp_phy_ant_gpio_config_t *out) {
    assert(locks==1);++reads;++gpio_reads;*out=stored_gpio;return sdk_error;
}
'''

RESET = r'''
static void reset(void) {
    assert(!locks);memset(&s_radio,0,sizeof(s_radio));
    s_radio.driver_owned=s_radio.storage_configured=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.lease_identity=19;s_radio.wake_locks=3;s_radio.promiscuous_claimed=true;
    memset(&stored_ant,0,sizeof(stored_ant));memset(&stored_gpio,0,sizeof(stored_gpio));
    stored_ant.rx_ant_mode=stored_ant.tx_ant_mode=ESP_PHY_ANT_MODE_AUTO;
    stored_ant.rx_ant_default=ESP_PHY_ANT_ANT1;stored_ant.enabled_ant0=15;stored_ant.enabled_ant1=14;
    for(unsigned i=0;i<4;++i){stored_gpio.gpio_cfg[i].gpio_select=i%2;stored_gpio.gpio_cfg[i].gpio_num=127-i;}
    reads=ant_reads=gpio_reads=0;sdk_error=0;
}
'''

MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_antenna_snapshot_t out,zero={0};const char *stage;
    reset();assert(esp32_mquickjs_wifi_radio_read_antenna(false,NULL,&stage)==ESP_ERR_INVALID_ARG && !reads);
    memset(&out,0xff,sizeof(out));
    assert(esp32_mquickjs_wifi_radio_read_antenna(false,&out,NULL)==ESP_ERR_INVALID_ARG && !memcmp(&out,&zero,sizeof(out)) && !reads);
    for(unsigned gpio=0;gpio<2;++gpio) {
        reset();unsigned char before[sizeof(s_radio)];memcpy(before,&s_radio,sizeof(s_radio));
        assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==ESP_OK && reads==1);
        assert(ant_reads==!gpio && gpio_reads==gpio && !memcmp(before,&s_radio,sizeof(s_radio)));
        if(gpio) {
            for(unsigned i=0;i<4;++i)assert(out.gpio.gpio_cfg[i].gpio_select==i%2 && out.gpio.gpio_cfg[i].gpio_num==127-i);
            memset(&stored_gpio,0,sizeof(stored_gpio));assert(out.gpio.gpio_cfg[0].gpio_num==127);
        } else {
            assert(out.config.enabled_ant0==15 && out.config.enabled_ant1==14);
            stored_ant.enabled_ant0=0;assert(out.config.enabled_ant0==15);
        }
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==ESP_OK);
        sdk_error=77;memset(&out,0xff,sizeof(out));
        assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==77 && !memcmp(&out,&zero,sizeof(out)));
        assert(!strcmp(stage,gpio ? "antenna-gpio" : "antenna"));
        for(unsigned bad=0;bad<8;++bad) {
            reset();switch(bad) {
                case 0:s_radio.driver_owned=false;break;
                case 1:s_radio.storage_configured=false;break;
                case 2:s_radio.lifecycle.identity=1;break;
                case 3:s_radio.operation.identity=1;break;
                case 4:s_radio.fault_stage="test";break;
                case 5:s_radio.cleanup_stage="test";break;
                case 6:s_radio.restart_required=true;break;
                case 7:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTING;break;
            }
            memcpy(before,&s_radio,sizeof(s_radio));memset(&out,0xff,sizeof(out));
            assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==(bad ? ESP_ERR_INVALID_STATE : ESP_ERR_WIFI_NOT_INIT));
            assert(!reads && !memcmp(&out,&zero,sizeof(out)) && !strcmp(stage,"admission") && !memcmp(before,&s_radio,sizeof(s_radio)));
        }
        for(unsigned state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;state<=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;++state) {
            if(state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED || state==ESP32_MQUICKJS_WIFI_RADIO_STARTED)continue;
            reset();s_radio.driver_state=state;memset(&out,0xff,sizeof(out));
            assert(esp32_mquickjs_wifi_radio_read_antenna(gpio,&out,&stage)==ESP_ERR_INVALID_STATE);
            assert(!reads && !memcmp(&out,&zero,sizeof(out)));
        }
    }
    for(unsigned bad=0;bad<7;++bad) {
        reset();switch(bad) {
            case 0:stored_ant.rx_ant_mode=ESP_PHY_ANT_MODE_MAX;break;
            case 1:stored_ant.tx_ant_mode=ESP_PHY_ANT_MODE_MAX;break;
            case 2:stored_ant.rx_ant_default=ESP_PHY_ANT_MAX;break;
            case 3:stored_ant.rx_ant_mode=-1;break;
            case 4:stored_ant.tx_ant_mode=-1;break;
            case 5:stored_ant.rx_ant_default=-1;break;
            case 6:stored_ant.rx_ant_mode=ESP_PHY_ANT_MODE_ANT0;break;
        }
        assert(esp32_mquickjs_wifi_radio_read_antenna(false,&out,&stage)==ESP_ERR_INVALID_RESPONSE);
        assert(!memcmp(&out,&zero,sizeof(out)) && !strcmp(stage,"decode"));
    }
    for(unsigned rx=0;rx<3;++rx)for(unsigned tx=0;tx<3;++tx)for(unsigned def=0;def<2;++def)for(unsigned selector=0;selector<16;++selector) {
        if(tx==ESP_PHY_ANT_MODE_AUTO && rx!=ESP_PHY_ANT_MODE_AUTO)continue;
        reset();stored_ant.rx_ant_mode=rx;stored_ant.tx_ant_mode=tx;stored_ant.rx_ant_default=def;
        stored_ant.enabled_ant0=selector;stored_ant.enabled_ant1=15-selector;
        assert(esp32_mquickjs_wifi_radio_read_antenna(false,&out,&stage)==ESP_OK);
        assert(out.config.rx_ant_mode==rx && out.config.tx_ant_mode==tx && out.config.rx_ant_default==def);
        assert(out.config.enabled_ant0==selector && out.config.enabled_ant1==15-selector);
    }
    assert(!locks);return 0;
}
'''

VM_MAIN = r'''
static void string_is(JSContext *ctx,JSValue value,const char *expected) {
    JSCStringBuf b;const char *text=JS_ToCString(ctx,value,&b);assert(text && !strcmp(text,expected));
}
static void number_is(JSContext *ctx,JSValue value,int expected) {
    int32_t number;assert(!JS_ToInt32(ctx,&number,value) && number==expected);
}
int main(int argc,char **argv) {
    assert(argc==3);bool gpio=atoi(argv[1]),failed=!strcmp(argv[2],"sdk-error"),arity=!strcmp(argv[2],"arity");
    bool admission=!strcmp(argv[2],"admission"),decode=!strcmp(argv[2],"decode");int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(96*1024);JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,value_ref,item_ref;
        JSValue *result=JS_PushGCRef(ctx,&result_ref),*value=JS_PushGCRef(ctx,&value_ref),*item=JS_PushGCRef(ctx,&item_ref);
        reset();sdk_error=failed ? 77 : 0;if(admission)s_radio.operation.identity=3;if(decode)stored_ant.rx_ant_mode=99;
        unsigned char saved_radio[sizeof(s_radio)],saved_ant[sizeof(stored_ant)],saved_gpio[sizeof(stored_gpio)];
        memcpy(saved_radio,&s_radio,sizeof(s_radio));memcpy(saved_ant,&stored_ant,sizeof(stored_ant));memcpy(saved_gpio,&stored_gpio,sizeof(stored_gpio));
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=(gpio ? js_wifi_driver_get_antenna_gpio : js_wifi_driver_get_antenna)(ctx,NULL,arity ? 1 : 0,value);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*result)==(failed || arity || admission || decode));}
        else assert(JS_IsException(*result));
        assert(reads==((arity || admission) ? 0 : 1));
        assert(!memcmp(saved_radio,&s_radio,sizeof(s_radio)) && !memcmp(saved_ant,&stored_ant,sizeof(stored_ant)) && !memcmp(saved_gpio,&stored_gpio,sizeof(stored_gpio)));
        if(JS_IsException(*result)) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && !arity) {
                *value=JS_GetPropertyStr(ctx,*result,"code");string_is(ctx,*value,"WIFI_DRIVER_READ_FAILED");
                *value=JS_GetPropertyStr(ctx,*result,"operation");string_is(ctx,*value,gpio ? "wifi.driver.getAntennaGpio" : "wifi.driver.getAntenna");
                *result=JS_GetPropertyStr(ctx,*result,"details");
                *value=JS_GetPropertyStr(ctx,*result,"espCode");number_is(ctx,*value,failed ? 77 : admission ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_RESPONSE);
                *value=JS_GetPropertyStr(ctx,*result,"stage");string_is(ctx,*value,admission ? "admission" : decode ? "decode" : gpio ? "antenna-gpio" : "antenna");
                *value=JS_GetPropertyStr(ctx,*result,"interface");assert(JS_IsNull(*value));
            }
        } else if(gpio) {
            *result=JS_GetPropertyStr(ctx,*result,"gpios");*value=JS_GetPropertyStr(ctx,*result,"length");number_is(ctx,*value,4);
            for(unsigned i=0;i<4;++i) {
                *item=JS_GetPropertyUint32(ctx,*result,i);*value=JS_GetPropertyStr(ctx,*item,"selected");assert(*value==(i%2 ? JS_TRUE : JS_FALSE));
                *value=JS_GetPropertyStr(ctx,*item,"gpio");number_is(ctx,*value,127-i);
            }
        } else {
            *value=JS_GetPropertyStr(ctx,*result,"rxMode");string_is(ctx,*value,"auto");
            *value=JS_GetPropertyStr(ctx,*result,"txMode");string_is(ctx,*value,"auto");
            *value=JS_GetPropertyStr(ctx,*result,"rxDefault");string_is(ctx,*value,"ant1");
            *value=JS_GetPropertyStr(ctx,*result,"enabledAnt0");number_is(ctx,*value,15);
            *value=JS_GetPropertyStr(ctx,*result,"enabledAnt1");number_is(ctx,*value,14);
        }
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&value_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks);free(heap);
    }
    return 0;
}
'''
