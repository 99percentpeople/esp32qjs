"""Deferred Action public capture/result tests against the production adapter.

Uses the real MQuickJS/ByteView and production option/copy/converter functions.
Allocation boundaries inject Nth failures and moving GC. No SDK/RF behavior or
Future scheduler/Radio concurrency is simulated by this fixture.
"""
import re
import tempfile
import unittest
from test_wifi_config_controls import sdk_types, structure
from test_wifi_rx_target import INTERNAL, unit
from wireless_vm_fixture import ROOT, CORE, build, extract, run

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_action.c'


class WiFiActionCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text()
        code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 0\n#define CONFIG_IDF_TARGET "fixture"\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n'
        code += sdk_types('esp32c5/representative', ('wifi_action_tx_req_t', 'wifi_action_tx_status_type_t', 'wifi_roc_req_t'))
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_action_lane.h')
        code += structure((INTERNAL / 'esp32_mquickjs_wifi_roc_session.h').read_text(), 'esp32_mquickjs_wifi_roc_status_t')
        code += structure(source, 'action_options_t')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += BOUNDARIES
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ('action_hex', 'action_mac', 'action_capture_options', 'action_capture_payload',
                     'action_capture', 'action_finish', 'js_wifi_action_capabilities',
                     'esp32_mquickjs_wifi_action_status'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_payload_options_gc_allocation_and_throwing_getters(self):
        base = 'channel:6,destination:"02:00:00:00:00:01",'
        cases = [
            ('({' + base + 'payload:view})', 1),
            ('({' + base + 'payload:[4],waitMs:60000,timeoutMs:1,noAck:true})', 1),
            ('({' + base + 'payload:[4],interface:"access-point",bssid:"FF:ff:ff:ff:ff:ff",secondaryChannel:"above"})', 1),
            ('({channel:6,destination:"00:00:00:00:00:00",payload:[4]})', 0),
            ('({channel:6,destination:"02:00:00:00:00:01\\u0000",payload:[4]})', 0),
            ('({' + base + 'payload:[]})', 0),
            ('({' + base + 'payload:[256]})', 0),
            ('({' + base + 'payload:[1.5]})', 0),
            ('({' + base + 'payload:{length:4294967295}})', 0),
            ('({' + base + 'payload:{length:1477}})', 0),
            ('({' + base + 'payload:view,timeoutMs:0})', 0),
            ('({' + base + 'payload:view,waitMs:60001})', 0),
            ('({' + base + 'payload:view,noAck:1})', 0),
            ('({' + base + 'payload:view,extra:true})', 0),
            ('({' + base + 'payload:view,interface:"station\\u0000"})', 0),
            ('({' + base + 'payload:view,get waitMs(){view.close();return 100;}})', 0),
            ('({' + base + 'get payload(){throw new Error("sentinel");}})', 0),
            ('({' + base + 'payload:{get length(){throw new Error("sentinel");}}})', 0),
            ('({' + base + 'payload:{length:1,get 0(){throw new Error("sentinel");}}})', 0),
            ('(function(){var n=0;return {' + base + 'get payload(){if(++n!==1)throw new Error("twice");return [4];}};})()', 1),
            ('(function(){var a=[];for(var i=0;i<1476;i++)a.push(4);return {' + base + 'payload:a};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected),
                     'sentinel' if 'sentinel' in expression else ''])

    def test_result_status_and_capabilities_gc_allocation(self):
        for mode in ('finish', 'status', 'capabilities'):
            with self.subTest(mode=mode):
                run([str(self.binary), mode, '', '1', ''])


BOUNDARIES = r'''
#define ESP32_MQUICKJS_WIFI_ROC_MAX_HANDLES 8
static bool esp32_mquickjs_wifi_roc_current_status(esp32_mquickjs_wifi_roc_status_t *out) {(void)out;return false;}
static void esp32_mquickjs_wifi_roc_counts(uint32_t *handles,bool *active,bool *cleanup) {*handles=0;*active=false;*cleanup=false;}
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#undef esp32_mquickjs_memory_wireless_alloc
static void *esp32_mquickjs_memory_wireless_alloc(const char *tag,size_t length,int policy,
    esp32_mquickjs_memory_budget_role_t role) {
    assert(!strcmp(tag,"wifi.action") && role==ESP32_MQUICKJS_MEMORY_BUDGET_TX && length>sizeof(wifi_action_tx_req_t) &&
        length<=sizeof(wifi_action_tx_req_t)+1476 && policy==1);
    return heap_caps_malloc(length,1);
}
static int esp32_mquickjs_wifi_action_receive(uint8_t *a,uint8_t *b,size_t c,uint8_t d) {
    (void)a;(void)b;(void)c;(void)d;return 0;
}
static JSValue action_error(JSContext *ctx,const char *code,esp_err_t error,const char *stage) {
    (void)error;(void)stage;return JS_ThrowTypeError(ctx,"%s",code);
}
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static const char s_action_retired_lock;
static struct {bool active;esp_err_t error;const char *stage;} s_action_retired={true,23,"terminal-fence"};
void esp32_mquickjs_wifi_radio_action_snapshot(esp32_mquickjs_wifi_action_lane_t *out) {
    *out=(esp32_mquickjs_wifi_action_lane_t){.identity=45,.generation=7,.operation_id=255,
        .submitted=true,.terminal=true,.cancel_written=true,.next_identity=0};
}
'''
MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,arg,result_ref,message_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JS_PushGCRef(ctx,&arg);JSValue *message=JS_PushGCRef(ctx,&message_ref);
        uint8_t *payload=heap_caps_malloc(1,1);payload[0]=4;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,payload,1);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        if(!strcmp(argv[1],"capture")) {
            arg.val=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(arg.val));
        }
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(!strcmp(argv[1],"capture")) {
            esp32_mquickjs_future_driver_state_t *state=NULL;
            ok=action_capture(ctx,NULL,1,&arg,&state);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok) {
                assert(state && !state->token.identity && !state->submitted);
                assert(state->request_bytes==sizeof(*state->request)+state->request->data_len);
                assert(state->request->data[0]==4 && state->request->channel==6);
                assert(state->request->rx_cb==esp32_mquickjs_wifi_action_receive);
                assert(state->request->type==WIFI_OFFCHAN_TX_REQ && !state->request->op_id);
                esp32_mquickjs_memory_payload_free(state->request);heap_caps_free(state);
            } else assert(!state && JS_HasException(ctx));
        } else {
            wifi_action_tx_req_t request={.ifx=WIFI_IF_STA,.channel=6,.data_len=1};
            esp32_mquickjs_future_driver_state_t state={.request=&request,.token={.generation=7,.identity=45},
                .result={.tx_status=WIFI_ACTION_TX_DONE,.terminal_status=WIFI_ACTION_TX_DURATION_COMPLETED,.operation_id=255}};
            *result=!strcmp(argv[1],"finish") ? action_finish(ctx,&state) : !strcmp(argv[1],"status") ?
                esp32_mquickjs_wifi_action_status(ctx) : js_wifi_action_capabilities(ctx,NULL,0,NULL);
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
            if(ok && !strcmp(argv[1],"status")) {
                assert(JS_GetPropertyStr(ctx,*result,"cleanupPending")==JS_TRUE);
                assert(JS_GetPropertyStr(ctx,*result,"identityExhausted")==JS_TRUE);
                int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"operationId")) && n==255);
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
        assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&message_ref);JS_PopGCRef(ctx,&arg);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
