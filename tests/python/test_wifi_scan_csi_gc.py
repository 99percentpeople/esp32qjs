"""Deferred actual scan input/result and CSI native result under moving GC/OOM."""
import re
import tempfile
import unittest
from test_wifi_config_controls import sdk_types
from test_wifi_driver_phy import COMPONENT
from wireless_vm_fixture import CORE, build, extract, run


class WiFiScanCsiGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        csi = (COMPONENT / 'src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        scan = (COMPONENT / 'internal/esp32_mquickjs_wifi_scan_parameters.h').read_text()
        scan = re.sub(r'^#(?:include|pragma).*$', '', scan, flags=re.M)
        cls.binaries = []
        for he, profile in ((False, 'esp32c3/representative'), (True, 'esp32c5/representative')):
            body = f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(he)}\n#define CONFIG_SOC_WIFI_MAC_VERSION_NUM {3 if he else 1}\n'
            body += sdk_types(profile, ('wifi_scan_default_params_t', 'wifi_csi_config_t'))
            body += BOUNDARIES + scan + options
            body += extract(driver, 'driver_scan_parameters_capture') + extract(driver, 'driver_scan_parameters_to_js')
            body += extract(csi, 'wifi_csi_native_config_to_js')
            cls.binaries.append(build(cls.temp.name + '/' + str(he), body, MAIN))

    def test_real_capture_converters_getters_nth_allocation_and_gc(self):
        for binary in self.binaries:
            run([str(binary)])


BOUNDARIES = r'''
#define WIFI_SCAN_PARAMS_DEFAULT_CONFIG() {.scan_time={.active={.min=0,.max=120},.passive=360},.home_chan_dwell_time=30}
#define ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE 2
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP_ERR_NOT_SUPPORTED 3
typedef int esp_err_t;
static int esp_wifi_set_scan_parameters(const wifi_scan_default_params_t *p){(void)p;abort();}
static JSValue wifi_csi_throw_driver_error(JSContext *ctx,const char *code,const char *stage,esp_err_t error){
    return JS_ThrowTypeError(ctx,"%s %s %d",code,stage,error);}
'''

MAIN = r'''
int main(void){
    for(unsigned mode=0;mode<2;++mode){
        unsigned total=1;
        for(unsigned nth=0;nth<=total;++nth){
            void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
            JSGCRef result_ref,item_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*item=JS_PushGCRef(ctx,&item_ref);
            wifi_scan_default_params_t scan=WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
            wifi_csi_config_t native={0};
#if CONFIG_SOC_WIFI_HE_SUPPORT
            native.enable=1;native.acquire_csi_vht=1;native.acquire_csi_force_lltf=1;
            native.acquire_csi_he_stbc_mode=2;native.lltf_bit_mode=0;native.val_scale_cfg=7;
#endif
            calls=0;fail_at=nth;inject=true;collect=true;
            *result=mode==0?driver_scan_parameters_to_js(ctx,&scan):wifi_csi_native_config_to_js(ctx,&native,17);
            inject=false;collect=false;
            if(!nth)total=calls;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else if(mode==0){int32_t number;*item=JS_GetPropertyStr(ctx,*result,"activeMaxMs");
                assert(!JS_ToInt32(ctx,&number,*item) && number==120);}
            else {
#if CONFIG_SOC_WIFI_HE_SUPPORT
                *item=JS_GetPropertyStr(ctx,*result,"capture");
                *result=JS_GetPropertyStr(ctx,*item,"lltfBits");int32_t bits;
                assert(!JS_ToInt32(ctx,&bits,*result) && bits==12);
                *result=JS_GetPropertyStr(ctx,*item,"vht");assert(JS_IsBool(*result) && JS_VALUE_GET_INT(*result));
#else
                assert(0); /* Unsupported targets must never fabricate a config. */
#endif
            }
            const char *text="({activeMinMs:121,activeMaxMs:0,passiveMs:0,homeChannelDwellMs:0})";
            *result=JS_Eval(ctx,text,strlen(text),"scan-input.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
            assert(!driver_scan_parameters_capture(ctx,*result,&scan));JS_GetException(ctx);
            text="({activeMinMs:0,activeMaxMs:0,passiveMs:0,homeChannelDwellMs:0})";
            *result=JS_Eval(ctx,text,strlen(text),"scan-input.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
            calls=0;fail_at=nth;inject=true;collect=true;
            bool captured=driver_scan_parameters_capture(ctx,*result,&scan);
            inject=false;collect=false;
            if(!nth){assert(captured);if(calls>total)total=calls;}
            if(!captured){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else assert(scan.scan_time.active.min==0 && scan.home_chan_dwell_time==0);

            JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&result_ref);
            JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
        }
    }
    return 0;
}
'''
