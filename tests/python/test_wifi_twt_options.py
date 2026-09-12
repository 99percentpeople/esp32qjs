"""Deferred production TWT capture/validation with real MQuickJS getters and GC.

Property lookup is an injected GC/failure boundary; it roots its by-value input
like the VM API. No SDK operation/session or alternate request state machine.
"""
from pathlib import Path
import re
import tempfile
import unittest
from test_wifi_rx_target import unit, INTERNAL
from test_wifi_config_controls import structure
from wireless_vm_fixture import ROOT, CORE, build, run


class WiFiTwtOptions(unittest.TestCase):
    def test_native_timing_bounds_getter_gc_and_capture_failure(self):
        header = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        extra = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n'
        extra += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', header).group(0)
        extra += structure(header, 'wifi_twt_setup_config_t')
        extra += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        extra += structure(header, 'wifi_btwt_setup_config_t')
        extra += unit(INTERNAL / 'esp32_mquickjs_wifi_twt_options.h')
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += BOUNDARY
        extra += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_twt/esp32_mquickjs_wifi_twt_options.c')
        cases = [
            ('i', '{}', True),
            ('i', 'undefined', False), ('i', 'null', False), ('i', '[]', False),
            ('i', '{command:"demand",flowId:7,connectionId:32767,trigger:false,announced:false,wakeDurationUnit:"1024us",minimumWakeDuration:255,wakeIntervalExponent:20,wakeIntervalMantissa:32768,responseTimeoutMs:65535,timeoutMs:60000}', True),
            ('i', '{connectionId:0}', True), ('i', '{connectionId:32768}', False),
            ('i', '{flowId:8}', False), ('i', '{flowId:-1}', False),
            ('i', '{flowId:1.5}', False), ('i', '{flowId:"1"}', False),
            ('i', '{flowId:NaN}', False), ('i', '{timeoutMs:Infinity}', False),
            ('i', '{responseTimeoutMs:99}', False), ('i', '{responseTimeoutMs:100}', True),
            ('i', '{responseTimeoutMs:65536}', False), ('i', '{timeoutMs:0}', False),
            ('i', '{timeoutMs:60001}', False), ('i', '{timeoutMs:1,responseTimeoutMs:65535}', True),
            ('i', '{minimumWakeDuration:1,wakeIntervalMantissa:10256,wakeIntervalExponent:0}', True),
            ('i', '{minimumWakeDuration:1,wakeIntervalMantissa:10255,wakeIntervalExponent:0}', False),
            ('i', '{wakeDurationUnit:"1024us",minimumWakeDuration:1,wakeIntervalMantissa:11024,wakeIntervalExponent:0}', True),
            ('i', '{wakeIntervalMantissa:16,wakeIntervalExponent:31}', True),
            ('i', '{wakeIntervalMantissa:17,wakeIntervalExponent:31}', False),
            ('i', '{wakeIntervalMantissa:65535,wakeIntervalExponent:31}', False),
            ('i', '{wakeIntervalExponent:32}', False), ('i', '{minimumWakeDuration:256}', False),
            ('i', '{trigger:1}', False), ('i', '{announced:"true"}', False),
            ('i', '{command:"accept"}', False), ('i', '{command:"request\\x00x"}', False),
            ('i', '{wakeDurationUnit:"tu"}', False), ('i', '{implicit:true}', False),
            ('i', '{get command(){gc();return "suggest";},get connectionId(){gc();return 31;},get trigger(){gc();return false;}}', True),
            ('i', '{get flowId(){throw 12345;}}', False),
            ('i', '(function(){var reads=0;return {get connectionId(){gc();if(++reads!==1)throw 12345;return 9;}};})()', True),
            ('b', '{broadcastId:1}', True), ('b', '{broadcastId:31,responseTimeoutMs:1,timeoutMs:1}', True),
            ('b', '{}', False), ('b', '{broadcastId:0}', False), ('b', '{broadcastId:32}', False),
            ('b', '{broadcastId:255}', False), ('b', '{broadcastId:1,flowId:0}', False),
            ('b', '{broadcastId:1,responseTimeoutMs:0}', False),
            ('b', '{broadcastId:1,responseTimeoutMs:65536}', False),
            ('b', '{command:"demand",get broadcastId(){gc();return 12;}}', True),
            ('b', '{get broadcastId(){throw 12345;}}', False),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, extra, MAIN)
            for kind, expression, valid in cases:
                run([str(binary), kind, expression, str(int(valid))])


BOUNDARY = r'''
static JSValue twt_get_property(JSContext *ctx,JSValue object,const char *key){
    JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&ref);return result;
}
#define JS_GetPropertyStr twt_get_property
'''
MAIN = r'''
static void native_bounds(void){
    esp32_mquickjs_wifi_itwt_options_t o={.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=1};
    assert(esp32_mquickjs_wifi_itwt_options_valid(&o));
    assert(esp32_mquickjs_wifi_itwt_duration_us(&o.config)==256 && esp32_mquickjs_wifi_itwt_interval_us(&o.config)==10256);
    o.config.wake_invl_mant--;assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));o.config.wake_invl_mant++;
    o.config.reserved=1;assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));o.config.reserved=0;
    o.config.twt_id=32768;assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));o.config.twt_id=0;
    o.config.wake_invl_expn=31;o.config.wake_invl_mant=16;
    assert(esp32_mquickjs_wifi_itwt_interval_us(&o.config)==(UINT64_C(1)<<35));
    assert(esp32_mquickjs_wifi_itwt_options_valid(&o));
    o.config.wake_invl_mant=65535;
    assert(esp32_mquickjs_wifi_itwt_interval_us(&o.config)==(UINT64_C(65535)<<31));
    assert(!esp32_mquickjs_wifi_itwt_options_valid(&o));
    assert(!esp32_mquickjs_wifi_itwt_options_valid(NULL) && !esp32_mquickjs_wifi_btwt_options_valid(NULL));
}
int main(int argc,char **argv){
    assert(argc==4);native_bounds();bool individual=argv[1][0]=='i',expected=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref);
        char expression[4096];snprintf(expression,sizeof(expression),"(%s)",argv[2]);
        *input=JS_Eval(ctx,expression,strlen(expression),"twt-options",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        esp32_mquickjs_wifi_itwt_options_t i;esp32_mquickjs_wifi_btwt_options_t b;
        memset(&i,0xa5,sizeof(i));memset(&b,0xa5,sizeof(b));
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=individual?esp32_mquickjs_wifi_capture_itwt(ctx,*input,&i):esp32_mquickjs_wifi_capture_btwt(ctx,*input,&b);
        inject=collect=false;
        if(!nth){total=calls;assert(ok==expected);}else assert(!ok);
        if(ok){
            assert(individual?esp32_mquickjs_wifi_itwt_options_valid(&i):esp32_mquickjs_wifi_btwt_options_valid(&b));
            if(individual && !strcmp(argv[2],"{}"))assert(!i.connection_id_set && i.config.flow_type==0 && i.config.trigger==1 && i.timeout_ms==6000);
        }else{
            assert(JS_HasException(ctx));JSValue exception=JS_GetException(ctx);
            if(!nth && strstr(argv[2],"throw 12345") && !expected)assert(exception==JS_NewInt32(ctx,12345));
            const unsigned char *p=individual?(const unsigned char *)&i:(const unsigned char *)&b;
            for(size_t n=0;n<(individual?sizeof(i):sizeof(b));n++)assert(!p[n]);
        }
        JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);free(heap);
        assert(!root_count && !native_live);
    }
    return 0;
}
'''
