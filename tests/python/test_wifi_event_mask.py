"""Deferred production event-mask control: preserve control events and rollback safely."""
import re
import tempfile
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, build, extract, run


def mask_code(profile, ap=True):
    code = control_code(profile, ap) + BOUNDARIES
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    return code + extract(source, 'esp32_mquickjs_wifi_radio_event_mask') + RESET


class WiFiEventMask(unittest.TestCase):
    def test_real_mask_reserved_bits_readback_rollback_and_faults(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, mask_code(profile, ap) + MAIN)

    def test_public_strict_mask_arity_original_errors_and_gc(self):
        code = re.sub(r'\bcalls\b', 'native_calls', mask_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('driver_read_error', 'driver_phy_write_error', 'js_wifi_driver_get_event_mask', 'js_wifi_driver_set_event_mask'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('0', True), ('1', True), ('2', False), ('4294967295', False),
                                      ('-1', False), ('0.5', False), ('"1"', False), ('true', False), ('null', False), ('NaN', False)]:
                run([str(binary), expression, str(int(valid)), 'set'])
            for scenario in ('get', 'get-error', 'set-error', 'set-arity', 'get-arity'):
                run([str(binary), '0', '0' if scenario != 'get' else '1', scenario])


BOUNDARIES = r'''
#define WIFI_EVENT_MASK_AP_PROBEREQRECVED (1U << 0)
static uint32_t native_mask;
static bool corrupt_mask_readback,corrupt_mask_rollback;
static unsigned unsafe_writes;
static int esp_wifi_get_event_mask(uint32_t *out) {
    *out=native_mask;
    if(corrupt_mask_readback && !strcmp(result.stage,"event-mask-readback") &&
       (!result.rollback_attempted || corrupt_mask_rollback))*out^=1U;
    return sdk_step(false);
}
static int esp_wifi_set_event_mask(uint32_t mask) {
    if(mask & ~WIFI_EVENT_MASK_AP_PROBEREQRECVED)++unsafe_writes;
    native_mask=mask;return sdk_step(true);
}
'''

RESET = r'''
static void mask_reset(void) {reset();native_mask=1;corrupt_mask_readback=corrupt_mask_rollback=false;unsafe_writes=0;}
'''

MAIN = r'''
int main(void) {
    uint32_t actual;
#define GET() esp32_mquickjs_wifi_radio_event_mask(false,0,&actual,&result)
#define SET(value) esp32_mquickjs_wifi_radio_event_mask(true,value,&actual,&result)
    mask_reset();s_radio.configuration.error=123;assert(GET()==ESP_OK && actual==1 && calls==1 && !writes && s_radio.configuration.error==123);
    assert(SET(1)==ESP_OK && !result.mutation_attempted && !writes);
    assert(SET(0)==ESP_OK && actual==0 && writes==1 && !unsafe_writes);
    for(unsigned bit=1;bit<32;++bit) {unsigned before=calls;assert(SET(UINT32_C(1)<<bit)==ESP_ERR_INVALID_ARG && calls==before);}
    unsigned before=calls;assert(SET(UINT32_MAX)==ESP_ERR_INVALID_ARG && calls==before);
    mask_reset();assert(SET(0)==ESP_OK);unsigned total=calls;
    for(unsigned nth=1;nth<=total;++nth) {
        mask_reset();fail_at=nth;assert(SET(0)==77 && !actual && result.error==77 && !unsafe_writes);
        if(result.mutation_attempted)assert(result.rollback_complete && native_mask==1 && !s_radio.fault_stage);
        else assert(!writes && !result.rollback_attempted);
    }
    mask_reset();fail_at=total;assert(SET(0)==77);unsigned suffix=calls-total;
    for(unsigned nth=1;nth<=suffix;++nth) {
        mask_reset();fail_at=total;rollback_fail=nth;assert(SET(0)==77 && result.rollback_error==88);
        assert(s_radio.fault_error==77 && !result.rollback_complete && s_radio.cleanup_error==88 && !unsafe_writes);
        before=calls;fail_at=0;assert(SET(0)==ESP_ERR_INVALID_STATE && calls==before);
    }
    mask_reset();corrupt_mask_readback=true;assert(SET(0)==ESP_ERR_INVALID_RESPONSE && result.rollback_complete && native_mask==1);
    /* A persistent readback mismatch must retain both the first error and the
     * failed rollback, even though the injected setter restored the bits. */
    mask_reset();corrupt_mask_readback=corrupt_mask_rollback=true;
    assert(SET(0)==ESP_ERR_INVALID_RESPONSE && result.rollback_attempted && !result.rollback_complete);
    assert(result.rollback_error==ESP_ERR_INVALID_RESPONSE && native_mask==1);
    assert(s_radio.fault_error==ESP_ERR_INVALID_RESPONSE && s_radio.cleanup_error==ESP_ERR_INVALID_RESPONSE);
    assert(!strcmp(result.stage,"event-mask-readback") && !strcmp(result.rollback_stage,"rollback-event-mask-readback"));
    before=calls;assert(SET(0)==ESP_ERR_INVALID_STATE && calls==before && !unsafe_writes);
    mask_reset();native_mask=UINT32_MAX;assert(GET()==ESP_OK && actual==UINT32_MAX);
    assert(SET(0)==ESP_OK && native_mask==0 && !unsafe_writes);
    mask_reset();native_mask=UINT32_MAX;fail_at=2;assert(SET(0)==77 && !result.rollback_attempted && writes==1 && !unsafe_writes && s_radio.fault_error==77);
    mask_reset();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(SET(0)==ESP_OK); /* Interface mode and RF owners do not affect this observation-only mask. */
    mask_reset();s_wifi_state.scan_in_progress=true;s_radio.wake_locks=1;s_radio.promiscuous_claimed=true;s_tx_rate_lease.identity=99;
    s_radio.leases[3]=(wifi_radio_live_lease_t){21,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX};
    assert(SET(0)==ESP_OK && !unsafe_writes);
    mask_reset();s_radio.operation.identity=1;assert(SET(0)==ESP_ERR_INVALID_STATE && !calls);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(GET()==ESP_ERR_INVALID_STATE && !calls);s_radio.lifecycle.identity=0;
    s_radio.driver_owned=false;assert(GET()==ESP_ERR_WIFI_NOT_INIT && !calls);
    assert(!locks && !critical && !helper_locks);return 0;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]),get=!strncmp(argv[3],"get",3),failed=strstr(argv[3],"error")!=NULL,arity=strstr(argv[3],"arity")!=NULL;
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"mask",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        mask_reset();if(get)native_mask=UINT32_MAX;if(failed)native_fail_at=get ? 1 : 2;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=get ? js_wifi_driver_get_event_mask(ctx,NULL,arity ? 1 : 0,input) : js_wifi_driver_set_event_mask(ctx,NULL,arity ? 0 : 1,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity || (!expected && !failed))assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==77);
                *input=JS_GetPropertyStr(ctx,*output,"interface");assert(JS_IsNull(*input));
            }
        } else {
            double n;assert(!JS_ToNumber(ctx,&n,*output));assert(n==(get ? (double)UINT32_MAX : atof(argv[1])));
        }
        assert(!unsafe_writes);
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
'''
