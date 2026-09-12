"""Deferred production ROC option/status conversion with real moving-GC VM.

This fixture checks pure public capture/conversion. Native lifecycle is covered
by test_wifi_roc_session; full Future/VM/Radio integration remains phase testing.
"""
import re
import tempfile
import unittest
from test_wifi_config_controls import sdk_types, structure
from test_wifi_rx_target import INTERNAL, unit
from wireless_vm_fixture import ROOT, CORE, build, extract, run

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_roc.c'


class WiFiRocCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text()
        code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 0\ntypedef int esp_err_t;\n#define ESP_OK 0\n'
        code += sdk_types('esp32c5/representative', ('wifi_roc_req_t',))
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_action_lane.h')
        code += structure((INTERNAL / 'esp32_mquickjs_wifi_roc_session.h').read_text(), 'esp32_mquickjs_wifi_roc_status_t')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += 'static int esp32_mquickjs_wifi_action_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d) {(void)a;(void)b;(void)c;(void)d;return 0;}\n'
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        code += extract(source, 'roc_options')
        code += extract(source, 'roc_status_to_js')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_strict_bounds_gc_single_getter_and_exceptions(self):
        cases = [
            ('({channel:6,durationMs:100})', 1),
            ('({channel:14,durationMs:60000,timeoutMs:1,interface:"access-point",secondaryChannel:"below",allowBroadcast:true})', 1),
            ('({channel:6,durationMs:0})', 0),
            ('({channel:6,durationMs:60001})', 0),
            ('({channel:6,durationMs:1.5})', 0),
            ('({channel:6,durationMs:100,timeoutMs:0})', 0),
            ('({channel:36,durationMs:100})', 0),
            ('({channel:4294967295,durationMs:100})', 0),
            ('({channel:6,durationMs:100,allowBroadcast:1})', 0),
            ('({channel:6,durationMs:100,interface:"station\\u0000"})', 0),
            ('({channel:6,durationMs:100,secondaryChannel:"above\\u0000"})', 0),
            ('({channel:6,durationMs:100,extra:true})', 0),
            ('null', 0), ('[]', 0),
            ('({channel:6,get durationMs(){throw new Error("sentinel");}})', 0),
            ('(function(){var n=0;return {channel:6,get durationMs(){if(++n!==1)throw new Error("twice");return 100;}};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected),
                     'sentinel' if 'sentinel' in expression else ''])

    def test_status_gc_and_each_conversion_allocation(self):
        run([str(self.binary), 'status', '', '1', ''])


MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref,result_ref,message_ref;
        JSValue *root=JS_PushGCRef(ctx,&root_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JSValue *message=JS_PushGCRef(ctx,&message_ref);
        if(!strcmp(argv[1],"capture")) {
            *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        }
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(!strcmp(argv[1],"capture")) {
            wifi_roc_req_t request={0};uint32_t timeout=0;
            ok=roc_options(ctx,&root_ref,&request,&timeout);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok) {
                assert(request.rx_cb==esp32_mquickjs_wifi_action_receive && !request.done_cb && !request.op_id);
                assert(request.type==WIFI_ROC_REQ && request.wait_time_ms>=1 && request.wait_time_ms<=60000);
                assert(timeout>=1 && timeout<=60000);
            } else assert(JS_HasException(ctx));
        } else {
            esp32_mquickjs_wifi_roc_status_t status={.request={.channel=6,.wait_time_ms=100},
                .started=true,.submitted=true,.close_requested=true,.retired=true,
                .native={.identity=45,.generation=7,.operation_id=255,.submitted=true,.terminal=true,.terminal_status=WIFI_ROC_DONE}};
            *result=roc_status_to_js(ctx,&status);ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
            if(ok) {
                assert(JS_GetPropertyStr(ctx,*result,"cleanupPending")==JS_FALSE);
                assert(JS_GetPropertyStr(ctx,*result,"driverAccepted")==JS_TRUE);
                int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"operationId")) && n==255);
                assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"sequence")) && n==45);
            }
        }
        inject=false;collect=false;
        if(!ok) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && argv[4][0]) {
                *message=JS_GetPropertyStr(ctx,*result,"message");
                JSCStringBuf buffer;size_t length;
                const char *text=JS_ToCStringLen(ctx,&length,*message,&buffer);
                assert(text && length==strlen(argv[4]) && !memcmp(text,argv[4],length));
            }
        }
        JS_PopGCRef(ctx,&message_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&root_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
