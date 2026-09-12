"""Deferred MQuickJS recovery capture/result/error rooting and Nth allocation.

Uses production capture/converters, with native state observations injected.
No fixture import, compilation or execution in the API implementation phase.
"""
import re
import tempfile
import unittest
from test_wifi_config_controls import structure
from test_wifi_configuration_cleanup import WIFI
from test_wifi_recovery_runtime import recovery_request_code
from test_wifi_rx_target import unit
from wireless_vm_fixture import CORE, INTERNAL, build, extract, run

SOURCE = WIFI.parent.parent / 'wifi_common/esp32_mquickjs_wifi_recovery.c'


class WiFiRecoveryCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text()
        header = (INTERNAL / 'esp32_mquickjs_wifi.h').read_text()
        code = recovery_request_code(twt=True) + BOUNDARIES
        code += structure(header, 'esp32_mquickjs_wifi_configuration_execution_t')
        code += structure(header, 'esp32_mquickjs_wifi_recovery_t')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ('recovery_method', 'recovery_code', 'recovery_whole_generation', 'recovery_capture_kind', 'recovery_capture', 'ftm_recovery_capture', 'raw_tx_recovery_capture', 'twt_recovery_capture', 'recovery_error', 'recovery_finish', 'recovery_on_timeout'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_immutable_identity_and_exception_allocation_paths(self):
        cases = [
            ('({sequence:51,radioGeneration:9})', 1),
            ('({sequence:4294967295,radioGeneration:4294967295,allowDisconnect:true,timeoutMs:60000})', 1),
            ('({sequence:0,radioGeneration:9})', 0),
            ('({sequence:1.5,radioGeneration:9})', 0),
            ('({sequence:51,radioGeneration:4294967296})', 0),
            ('({sequence:51})', 0),
            ('({sequence:51,radioGeneration:9,operationId:3})', 0),
            ('({sequence:51,radioGeneration:9,allowDisconnect:1})', 0),
            ('({sequence:51,radioGeneration:9,timeoutMs:0})', 0),
            ('({sequence:51,radioGeneration:9,get timeoutMs(){throw new Error("sentinel");}})', 0),
            ('(function(){var n=0;return {get sequence(){if(++n!==1)throw new Error("twice");return 51;},radioGeneration:9};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected)])
        for mode in ('finish', 'error', 'timeout'):
            run([str(self.binary), mode, '', '1'])

    def test_twt_generation_requires_both_consents_and_has_no_sequence(self):
        for expression, expected in [
            ('({radioGeneration:9,closeAll:true,allowDisconnect:true})', 1),
            ('({radioGeneration:9,closeAll:true,allowDisconnect:false})', 0),
            ('({radioGeneration:9,closeAll:true})', 0),
            ('({radioGeneration:9,allowDisconnect:true})', 0),
            ('({radioGeneration:9,closeAll:false,allowDisconnect:true})', 0),
            ('({radioGeneration:9,closeAll:1,allowDisconnect:true})', 0),
            ('({sequence:51,radioGeneration:9,closeAll:true,allowDisconnect:true})', 0),
            ('({radioGeneration:0,closeAll:true,allowDisconnect:true})', 0),
            ('({radioGeneration:9,closeAll:true,allowDisconnect:true,timeoutMs:60001})', 0),
            ('({get closeAll(){gc();return true;},get radioGeneration(){gc();return 9;},allowDisconnect:true})', 1),
        ]:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected), 'twt'])
        for mode in ('finish', 'error', 'timeout'):
            run([str(self.binary), mode, '', '1', 'twt'])


BOUNDARIES = r'''
typedef int esp_err_t,wifi_mode_t,wifi_config_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_STATE 2
typedef struct {unsigned generation,identity;} esp32_mquickjs_wifi_radio_lifecycle_t;
typedef struct {unsigned generation;bool restart_required;const char *fault_stage;int fault_error;} esp32_mquickjs_wifi_radio_status_t;
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *out) {
    *out=(esp32_mquickjs_wifi_radio_status_t){.generation=10};return ESP_OK;
}
static bool esp32_mquickjs_wifi_configuration_pending(void) {return true;}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) {return 0;}
static struct station_state {int sta_detach_error;} station;
static struct station_state *esp32_mquickjs_wifi_state(void) {return &station;}
static const char *esp_err_to_name(int error) {(void)error;return "injected-esp-error";}
static esp32_mquickjs_wifi_recovery_kind_t kind_case;
static bool timeout_case;
static const char *const methods[]={"wifi.action.recover","wifi.rawTx.recover","wifi.twt.recover","wifi.ftm.recover"};
static const char *const failures[]={"WIFI_ACTION_RECOVERY_FAILED","WIFI_RAW_TX_RECOVERY_FAILED","WIFI_TWT_RECOVERY_FAILED","WIFI_FTM_RECOVERY_FAILED"};
static const char *const timeouts[]={"WIFI_ACTION_RECOVERY_TIMEOUT","WIFI_RAW_TX_RECOVERY_TIMEOUT","WIFI_TWT_RECOVERY_TIMEOUT","WIFI_FTM_RECOVERY_TIMEOUT"};
static JSValue esp32_mquickjs_throw_native_error(JSContext *ctx,const char *code,const char *operation,const char *message,JSValue details) {
    assert(!strcmp(operation,methods[kind_case]));(void)message;
    assert(!strcmp(code,timeout_case?timeouts[kind_case]:failures[kind_case]));
    assert(JS_GetClassID(ctx,details)==JS_CLASS_OBJECT);return JS_ThrowTypeError(ctx,"%s",code);
}
'''

MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4 || argc==5);int total=0;bool capture=!strcmp(argv[1],"capture");
    const esp32_mquickjs_wifi_recovery_kind_t kinds[]={ESP32_MQUICKJS_WIFI_RECOVERY_ACTION,
        ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX,ESP32_MQUICKJS_WIFI_RECOVERY_FTM};
    for(int kind=0;kind<(argc==5?1:3);++kind) {
    kind_case=argc==5?ESP32_MQUICKJS_WIFI_RECOVERY_TWT:kinds[kind];timeout_case=!strcmp(argv[1],"timeout");total=0;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef arg,result_ref;JS_PushGCRef(ctx,&arg);JSValue *result=JS_PushGCRef(ctx,&result_ref);
        if(capture){arg.val=JS_Eval(ctx,argv[2],strlen(argv[2]),"capture",JS_EVAL_RETVAL);assert(!JS_IsException(arg.val));}
        calls=0;fail_at=nth;inject=collect=true;
        bool ok;
        if(capture) {
            esp32_mquickjs_future_driver_state_t *state=NULL;
            ok=kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_TWT?twt_recovery_capture(ctx,NULL,1,&arg,&state):
                kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_RAW_TX?raw_tx_recovery_capture(ctx,NULL,1,&arg,&state):
                kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_FTM?ftm_recovery_capture(ctx,NULL,1,&arg,&state):recovery_capture(ctx,NULL,1,&arg,&state);
            if(!nth){total=calls;assert(ok==(atoi(argv[3])!=0));}
            if(ok){assert(state && state->operation.kind==kind_case && !state->started && !state->recovery.execution.admitted && !state->recovery.access_point);heap_caps_free(state);}
            else assert(!state && JS_HasException(ctx));
        } else {
            esp32_mquickjs_future_driver_state_t state={.complete=true,.operation={9,51,kind_case}};
            if(kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_TWT)state.operation.identity=0;
            state.recovery.execution.admitted=true;state.recovery.execution.stage="recovery-native-drain";
            if(!strcmp(argv[1],"finish"))*result=recovery_finish(ctx,&state);
            else if(!strcmp(argv[1],"timeout"))*result=recovery_on_timeout(ctx,&state,1000);
            else{state.error=77;*result=recovery_error(ctx,&state,false);}
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok==!strcmp(argv[1],"finish"));}
            if(ok){int32_t value;assert(!JS_ToInt32(ctx,&value,JS_GetPropertyStr(ctx,*result,"radioGeneration")) && value==10);
                if(kind_case==ESP32_MQUICKJS_WIFI_RECOVERY_TWT)assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"sequence")));}
        }
        inject=collect=false;
        if(!ok){assert(JS_HasException(ctx));*result=JS_GetException(ctx);}
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&arg);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    }
    return 0;
}
'''
