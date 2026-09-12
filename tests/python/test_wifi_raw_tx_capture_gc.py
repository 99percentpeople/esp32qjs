"""Deferred real VM capture/conversion, Nth allocation and moving-GC coverage."""
import json
import re
import tempfile
import unittest
from test_wifi_tx_rate import rate_code

from test_wifi_config_controls import HEADER, structure
from test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit
from wireless_vm_fixture import CORE, build, extract, run

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


class WiFiRawTxCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx.c').read_text()
        symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']['esp32c5/representative']['symbols']
        types = {key.split('::')[-1]: value['declaration'] for key, value in symbols.items()}
        extra = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_IDF_TARGET "fixture"\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n'
        for name in ['wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t']:
            extra += types[name] + ';\n'
        extra += rate_code('esp32c5/representative', ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t')).replace('#define CONFIG_SOC_WIFI_SUPPORT_5G 1', '#define CONFIG_SOC_WIFI_SUPPORT_5G 0')
        for name in ['wifi_rx', 'wifi_raw_tx_validate', 'wifi_raw_tx_snapshot', 'wifi_raw_tx_broker', 'wifi_raw_tx_lane']:
            extra += unit(INTERNAL / ('esp32_mquickjs_' + name + '.h'))
        extra += structure((INTERNAL / 'esp32_mquickjs_wifi_raw_tx_session.h').read_text(), 'esp32_mquickjs_wifi_raw_tx_sessions_status_t')
        extra += structure((INTERNAL / 'esp32_mquickjs_wifi_raw_tx_periodic_job.h').read_text(), 'esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t')
        extra += '#define ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_MIN_INTERVAL_US 1000\n#define ESP32_MQUICKJS_WIFI_RAW_TX_MAX_PERIODIC_JOBS 8\n'
        extra += unit(COMMON / 'esp32_mquickjs_wifi_rx.c')
        extra += unit(RAW / 'esp32_mquickjs_wifi_raw_tx_validate.c')
        extra += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', HEADER.read_text()).group(0)
        extra += structure(HEADER.read_text(), 'esp32_mquickjs_wifi_radio_lease_t')
        extra += structure(source, 'raw_tx_options_t')
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        extra += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        extra += source[start:source.index('\n};', start) + 3]
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += BOUNDARIES
        extra += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ['raw_tx_capture_options', 'esp32_mquickjs_wifi_raw_tx_capture_bytes', 'raw_tx_capture_bytes', 'raw_tx_capture',
                     'esp32_mquickjs_wifi_raw_tx_result_to_js', 'raw_tx_finish', 'js_wifi_raw_tx_capabilities', 'esp32_mquickjs_wifi_raw_tx_status']:
            extra += extract(source, name)
        cls.binary = build(cls.temp.name, extra, MAIN)

    def test_capture_bounds_single_getters_byteview_close_exceptions_and_gc(self):
        packet = '(function(){var a=[];for(var i=0;i<24;i++)a.push(i===0?128:0);return a;})()'
        cases = [
            ('({frame:view})', 1),
            ('({frame:' + packet + ',options:{validation:"basic",timeoutMs:1}})', 1),
            ('({frame:view,options:{interface:"access-point",sequenceControl:"application",channel:14,timeoutMs:60000}})', 1),
            ('({frame:view,options:{channel:"current"}})', 1),
            ('({frame:view,options:{timeoutMs:0}})', 0),
            ('({frame:view,options:{timeoutMs:60001}})', 0),
            ('({frame:view,options:{timeoutMs:1.5}})', 0),
            ('({frame:view,options:{channel:36}})', 0),
            ('({frame:view,options:{interface:"station\\u0000"}})', 0),
            ('({frame:view,options:{extra:true}})', 0),
            ('({frame:view,options:null})', 0),
            ('({frame:view,options:[]})', 0),
            ('({frame:{length:4294967295}})', 0),
            ('({frame:{length:23}})', 0),
            ('({frame:{length:1501}})', 0),
            ('({frame:{length:24,0:256}})', 0),
            ('({frame:{length:24,0:1.5}})', 0),
            ('({frame:view,options:{get channel(){view.close();return "current";}}})', 0),
            ('({frame:{get length(){throw new Error("sentinel");}}})', 0),
            ('({frame:{length:24,get 0(){throw new Error("sentinel");}}})', 0),
            ('({frame:view,options:{get channel(){throw new Error("sentinel");}}})', 0),
            ('(function(){var n=0;var a=' + packet + ';var f={get length(){if(++n!==1)throw new Error("twice");return 24;}};for(var i=0;i<24;i++)f[i]=a[i];return {frame:f};})()', 1),
            ('(function(){var n=0;return {frame:view,options:{get channel(){if(++n!==1)throw new Error("twice");return "current";}}};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected),
                     'sentinel' if 'sentinel' in expression else ''])

    def test_result_and_capabilities_construction_allocation_failure_and_gc(self):
        run([str(self.binary), 'finish', '', '1', ''])
        run([str(self.binary), 'capabilities', '', '1', ''])
        run([str(self.binary), 'status', '', '1', ''])
        run([str(self.binary), 'terminated-status', '', '1', ''])


BOUNDARIES = r'''
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#undef esp32_mquickjs_memory_wireless_alloc
static void *esp32_mquickjs_memory_wireless_alloc(const char *tag,size_t length,int policy,int role) {
    assert(role==ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    assert(!strcmp(tag,"wifi.raw-tx") && length>=24 && length<=1500 && policy==1);
    return heap_caps_malloc(length,1);
}
static JSValue raw_tx_error(JSContext *ctx,const char *code,esp_err_t error,const char *stage,
                           esp32_mquickjs_wifi_raw_tx_validation_t validation) {
    (void)error;(void)stage;(void)validation;return JS_ThrowTypeError(ctx,"%s",code);
}
static const char *esp_get_idf_version(void) {return "recorded-fixture";}
/* Fixed native snapshots exercise the production status converter. Concurrency
 * of these boundaries is covered by separate broker/lane production fixtures. */
static const char s_retired_lock;
static struct {bool active;esp_err_t error;const char *stage;} s_retired={true,23,"radio-release-stop"};
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static bool terminated_snapshot;
void esp32_mquickjs_wifi_raw_tx_broker_status(esp32_mquickjs_wifi_raw_tx_broker_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_broker_status_t){.operation_active=!terminated_snapshot,.quarantined=true,
        .native_terminated=terminated_snapshot,.token={.identity=45,.generation=7},.submit_error=12,.cleanup_error=34};
}
void esp32_mquickjs_wifi_raw_tx_sessions_status(esp32_mquickjs_wifi_raw_tx_sessions_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_sessions_status_t){.live=2,.closing=1,.faulted=1,
        .pending_results=3,.pending_flushes=2,.error_generation=12,.error=45,.stage="submit"};
}
void esp32_mquickjs_wifi_raw_tx_periodic_jobs_status(esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t){.live=2,.cleanup_pending=1,.faulted=1,
        .error_generation=88,.error=12,.cleanup_error=34,.stage="timer-start",.cleanup_stage="timer-delete"};
}
void esp32_mquickjs_wifi_raw_tx_lane_status(esp32_mquickjs_wifi_raw_tx_lane_status_t *output) {
    *output=(esp32_mquickjs_wifi_raw_tx_lane_status_t){.active_identity=77,.waiting=8,.identity_exhausted=true};
}
'''

MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,args[2],root_ref,result_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*root=JS_PushGCRef(ctx,&root_ref);
        JSValue *result=JS_PushGCRef(ctx,&result_ref);
        JS_PushGCRef(ctx,&args[0]);JS_PushGCRef(ctx,&args[1]);
        uint8_t *payload=heap_caps_malloc(24,1);memset(payload,0,24);payload[0]=0x80;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,payload,24);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        if(!strcmp(argv[1],"capture")) {
            *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
            args[0].val=JS_GetPropertyStr(ctx,*root,"frame");args[1].val=JS_GetPropertyStr(ctx,*root,"options");
            assert(!JS_IsException(args[0].val) && !JS_IsException(args[1].val));
        }
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(!strcmp(argv[1],"capture")) {
            esp32_mquickjs_future_driver_state_t *state=NULL;
            ok=raw_tx_capture(ctx,NULL,2,args,&state);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok) {
                assert(state && state->length==24 && state->bytes[0]==0x80 && !state->lease.acquired);
                assert(state->validation==ESP32_MQUICKJS_WIFI_RAW_TX_VALID);
                esp32_mquickjs_memory_payload_free(state->bytes);heap_caps_free(state);
            } else assert(!state && JS_HasException(ctx));
        } else {
            esp32_mquickjs_future_driver_state_t state={0};
            state.result.frame_type=ESP32_MQUICKJS_WIFI_RAW_TX_BEACON;
            state.result.token.identity=7;state.result.token.generation=9;state.result.byte_length=24;
            state.result.driver_accepted=state.result.driver_completed=true;
            state.result.completion.status=ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS;
            terminated_snapshot=!strcmp(argv[1],"terminated-status");
            *result=!strcmp(argv[1],"finish") ? raw_tx_finish(ctx,&state) :
                (!strcmp(argv[1],"status") || terminated_snapshot) ? esp32_mquickjs_wifi_raw_tx_status(ctx) : js_wifi_raw_tx_capabilities(ctx,NULL,0,NULL);
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
            if(ok && !strcmp(argv[1],"finish")) {
                JSValue flag=JS_GetPropertyStr(ctx,*result,"driverCompleted");assert(flag==JS_TRUE);
            }
            if(ok && (!strcmp(argv[1],"status") || terminated_snapshot)) {
                int32_t number;
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"laneIdentity")) && number==77);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"operationIdentity")) && number==45);
                assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"laneWaiters")) && number==8);
                assert(JS_GetPropertyStr(ctx,*result,"laneIdentityExhausted")==JS_TRUE);
                assert(JS_GetPropertyStr(ctx,*result,"nativeTerminated")==JS_NewBool(terminated_snapshot));
                if(terminated_snapshot) {
                    assert(JS_GetPropertyStr(ctx,*result,"operationActive")==JS_FALSE);
                    assert(!JS_ToInt32(ctx,&number,JS_GetPropertyStr(ctx,*result,"terminatedRadioGeneration")) && number==7);
                } else assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"terminatedRadioGeneration")));
            }
        }
        inject=false;collect=false;
        if(!ok) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && argv[4][0]) {
                args[1].val=JS_GetPropertyStr(ctx,*result,"message");
                JSCStringBuf buffer;size_t length;
                const char *text=JS_ToCStringLen(ctx,&length,args[1].val,&buffer);
                assert(text && length==strlen(argv[4]) && !memcmp(text,argv[4],length));
            }
        }
        assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&args[1]);JS_PopGCRef(ctx,&args[0]);JS_PopGCRef(ctx,&result_ref);
        JS_PopGCRef(ctx,&root_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
