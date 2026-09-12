"""Deferred real MQuickJS HE option capture and nested native readback under GC/OOM."""
import re
import tempfile
import unittest
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from wireless_vm_fixture import CORE, build, extract, run


class WiFiHeStatisticsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_he_statistics.h').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        code = structure(header, 'esp32_mquickjs_wifi_he_statistics_t') + BOUNDARIES + options
        code += re.search(r'static const char \*const driver_statistics_categories\[\][^;]*;', driver).group(0)
        for name in ('driver_rx_statistics_capture', 'driver_rx_statistics_to_js', 'js_wifi_driver_get_statistics_config'):
            code += extract(driver, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_capture_and_native_result_with_moving_gc_and_nth_allocation_failure(self):
        run([str(self.binary)])


BOUNDARIES = r'''
typedef int esp_err_t;
#define ESP_OK 0
static unsigned native_reads;
static int esp32_mquickjs_wifi_radio_read_he_statistics(esp32_mquickjs_wifi_he_statistics_t *actual,const char **stage){
    ++native_reads;*actual=(esp32_mquickjs_wifi_he_statistics_t){true,false,9};*stage=NULL;return 0;}
static JSValue driver_read_error(JSContext *ctx,const char *iface,const char *op,int error,const char *stage){
    (void)iface;return JS_ThrowTypeError(ctx,"%s %d %s",op,error,stage);}
'''

MAIN = r'''
int main(void){
    unsigned total=1;
    for(unsigned nth=0;nth<=total;++nth){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,item_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*item=JS_PushGCRef(ctx,&item_ref);
        calls=0;fail_at=nth;inject=true;collect=true;
        *result=js_wifi_driver_get_statistics_config(ctx,NULL,0,NULL);
        inject=false;collect=false;if(!nth)total=calls;
        if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else{
            *item=JS_GetPropertyStr(ctx,*result,"rx");assert(JS_GetPropertyStr(ctx,*item,"ordinary")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*item,"multiUser")==JS_FALSE);
            *item=JS_GetPropertyStr(ctx,*result,"tx");assert(JS_GetPropertyStr(ctx,*item,"voice")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*item,"video")==JS_FALSE);
            assert(JS_GetPropertyStr(ctx,*item,"background")==JS_TRUE);
        }
        const char *text="({ordinary:true,multiUser:false})";
        *result=JS_Eval(ctx,text,strlen(text),"statistics.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
        uint8_t selection;calls=0;fail_at=nth;inject=true;collect=true;
        bool captured=driver_rx_statistics_capture(ctx,*result,&selection);
        inject=false;collect=false;
        if(!nth){assert(captured);if(calls>total)total=calls;}
        if(!captured){assert(JS_HasException(ctx));JS_GetException(ctx);}else assert(selection==1);
        const char *invalid[]={"({ordinary:1,multiUser:false})","({ordinary:true})",
            "({ordinary:true,multiUser:false,extra:true})","null"};
        for(unsigned i=0;i<4;++i){*result=JS_Eval(ctx,invalid[i],strlen(invalid[i]),"invalid.js",JS_EVAL_RETVAL);
            assert(!JS_IsException(*result));unsigned before=native_reads;
            assert(!driver_rx_statistics_capture(ctx,*result,&selection) && native_reads==before);JS_GetException(ctx);}
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
