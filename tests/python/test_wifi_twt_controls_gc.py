"""Deferred production TWT policy input/readback with real movable MQuickJS GC."""
import re
import tempfile
import unittest
from pathlib import Path
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from wireless_vm_fixture import CORE, build, extract, run


class WiFiTwtControlsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt.c').read_text()
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        code = structure(sdk, 'wifi_twt_config_t') + options
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        code += extract(source, 'twt_policy_capture') + extract(source, 'twt_policy_value')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_real_policy_capture_and_result_nth_allocation_gc(self):
        run([str(self.binary)])


MAIN = r'''
int main(void){
    unsigned total=1;
    for(unsigned nth=0;nth<=total;++nth){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);
        wifi_twt_config_t config={true,false};
        calls=0;fail_at=nth;inject=true;collect=true;*result=twt_policy_value(ctx,&config);
        inject=false;collect=false;if(!nth)total=calls;
        if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else{assert(JS_GetPropertyStr(ctx,*result,"postWakeupEvents")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*result,"keepAlive")==JS_FALSE);}
        const char *text="({postWakeupEvents:false,keepAlive:true})";
        *result=JS_Eval(ctx,text,strlen(text),"twt-policy.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
        calls=0;fail_at=nth;inject=true;collect=true;bool captured=twt_policy_capture(ctx,*result,&config);
        inject=false;collect=false;
        if(!nth){assert(captured);if(calls>total)total=calls;}
        if(!captured){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else assert(!config.post_wakeup_event && config.twt_enable_keep_alive);
        const char *invalid[]={"({postWakeupEvents:1,keepAlive:false})","({postWakeupEvents:true})",
            "({postWakeupEvents:true,keepAlive:false,extra:1})","[]","null"};
        for(unsigned i=0;i<5;++i){*result=JS_Eval(ctx,invalid[i],strlen(invalid[i]),"invalid.js",JS_EVAL_RETVAL);
            assert(!JS_IsException(*result));assert(!twt_policy_capture(ctx,*result,&config));JS_GetException(ctx);}
        JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
