"""Deferred real restart capture/binding/error conversion with native boundaries.

Native execution and wait scopes are injected here. The production Radio gate,
runtime executor, phase sequence and actual wait budget have separate fixtures;
this checks JS admission, dispatch-once, diagnostics and GC/OOM, not reconstruction.
"""
import tempfile
import unittest

from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from wireless_vm_fixture import CORE, build, extract, run


class WiFiPublicRestart(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi.h').read_text()
        radio = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        config = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        code = 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 2\n'
        code += structure(header, 'esp32_mquickjs_wifi_configuration_execution_t')
        code += structure(radio, 'esp32_mquickjs_wifi_radio_config_result_t')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += BOUNDARIES
        code += ''.join(extract(config, name) for name in (
            'esp32_mquickjs_wifi_capture_restart',))
        code += ''.join(extract(wifi, name) for name in (
            'wifi_make_configuration_status', 'wifi_throw_restart_error', 'js_wifi_driver_restart'))
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_input_default_budget_and_nth_vm_allocation(self):
        for expression, expected in [
            ('undefined', 10000), ('({})', 10000), ('({timeoutMs:undefined})', 10000),
            ('({allowApRestart:undefined})', 10000), ('({allowApRestart:false})', 10000),
            ('({allowApRestart:1})', 0), ('({allowApRestart:null})', 0), ('({allowApRestart:"true"})', 0),
            ('({timeoutMs:1})', 1), ('({timeoutMs:60000})', 60000),
            ('null', 0), ('[]', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:-1})', 0),
            ('({timeoutMs:60001})', 0), ('({timeoutMs:1.5})', 0), ('({timeoutMs:"10"})', 0),
            ('({timeoutMs:4294967297})', 0), ('({timeoutMs:0/0})', 0), ('({timeoutMs:1/0})', 0),
            ('({"timeoutMs\\u0000":10})', 0), ('({force:true})', 0),
            ('({requireExclusive:false})', 0), ('({mode:"station"})', 0)]:
            with self.subTest(expression=expression):
                run([str(self.binary), expression, str(expected), '0'])

    def test_explicit_ap_option_survives_gc_and_nth_allocation(self):
        run([str(self.binary), '({timeoutMs:100,allowApRestart:true})', '100', '6'])

    def test_wait_rejection_admission_failure_current_call_details_and_gc(self):
        for scenario in (1, 2, 3, 4, 5, 7):
            run([str(self.binary), '({timeoutMs:100})', '100', str(scenario)])


BOUNDARIES = r'''
static struct {int sta_detach_error;} s_wifi_state;
static bool s_wifi_configuration_cleanup,wait_active,driver_started;
static unsigned wait_calls,end_calls,execute_calls,status_calls,diagnostic_calls;
static uint32_t waited_ms;
static int scenario;
typedef struct {
    const char *fault_stage;int fault_error;bool restart_required;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} esp32_mquickjs_wifi_radio_status_t;
static esp32_mquickjs_wifi_radio_status_t native_radio;
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *out) {
    assert(!wait_active);++diagnostic_calls;*out=native_radio;return ESP_OK;
}
static uint32_t esp32_mquickjs_wifi_radio_restart_snapshot_bytes(void) {
    return scenario>=3 && execute_calls ? 696 : 0;
}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) {return scenario==5 ? 88 : 0;}
static const char *esp_err_to_name(int error) {(void)error;return "injected-sdk-error";}
static int esp32_mquickjs_wifi_wait_begin(uint32_t timeout) {
    assert(!wait_active && !wait_calls);++wait_calls;waited_ms=timeout;
    if(scenario==1)return ESP_ERR_INVALID_STATE;wait_active=true;return ESP_OK;
}
static void esp32_mquickjs_wifi_wait_end(void) {
    assert(wait_active && execute_calls==1);wait_active=false;++end_calls;
}
static int esp32_mquickjs_wifi_restart_stopped_interfaces(
    bool allow_ap_restart, esp32_mquickjs_wifi_configuration_execution_t *execution) {
    assert(allow_ap_restart==(scenario==6));
    assert(wait_active && !execute_calls);++execute_calls;
    *execution=(esp32_mquickjs_wifi_configuration_execution_t){.stage="restart-admission"};
    if(scenario==2)return ESP_ERR_INVALID_STATE;
    execution->admitted=true;s_wifi_configuration_cleanup=true;
    if(scenario==7) {
        execution->stop_attempted=true;execution->stage="restart-retry-stop";
        native_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){0};
        native_radio.fault_stage="original-start";native_radio.fault_error=77;return 88;
    }
    execution->stage="restart-stopped-station-prepare";
    if(scenario==3)return 77;
    execution->stop_attempted=true;execution->stage="restart-checkpoint";
    native_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){.stage="restart-station-config",.error=77};
    if(scenario==4)return 77;
    execution->configuration_attempted=execution->resume_attempted=true;
    if(scenario==5){execution->stage="restart-resume";native_radio.fault_stage="start";native_radio.fault_error=77;return 77;}
    driver_started=true;s_wifi_configuration_cleanup=false;execution->stage="complete";return ESP_OK;
}
static JSValue wifi_make_status_object(JSContext *ctx) {
    assert(driver_started && !wait_active && execute_calls==1 && end_calls==1);++status_calls;
    return JS_NewObject(ctx);
}
'''


MAIN = r'''
static bool text_is(JSContext *ctx,JSValue object,const char *key,const char *expected) {
    JSCStringBuf b;JSValue value=JS_GetPropertyStr(ctx,object,key);size_t length;
    const char *s=JS_ToCStringLen(ctx,&length,value,&b);
    return s && length==strlen(expected) && !memcmp(s,expected,length);
}
int main(int argc,char **argv) {
    assert(argc==4);unsigned expected=atoi(argv[2]);scenario=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,result_ref,details_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&result_ref),*details=JS_PushGCRef(ctx,&details_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"restart",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        wait_calls=end_calls=execute_calls=status_calls=diagnostic_calls=0;waited_ms=0;
        wait_active=driver_started=s_wifi_configuration_cleanup=false;s_wifi_state.sta_detach_error=0;
        native_radio=(esp32_mquickjs_wifi_radio_status_t){.configuration={.stage="old-config",.error=88,.mutation_attempted=true}};
        assert(JS_IsException(js_wifi_driver_restart(ctx,NULL,2,input)) && !wait_calls && !execute_calls);JS_GetException(ctx);
        calls=0;fail_at=nth;inject=true;collect=true;
        *output=js_wifi_driver_restart(ctx,NULL,JS_IsUndefined(*input) ? 0 : 1,input);
        inject=false;collect=false;
        assert(!wait_active && execute_calls<=1 && end_calls==execute_calls && status_calls<=1);
        if(wait_calls)assert(waited_ms==expected);
        if(!expected)assert(!wait_calls && !execute_calls && !diagnostic_calls);
        if(!nth) {
            total=calls;
            assert(JS_IsException(*output)==(!expected || (scenario!=0 && scenario!=6)));
            if(expected && (!scenario || scenario==6))assert(driver_started && status_calls==1 && execute_calls==1);
            if(expected && scenario && scenario!=6) {
                *output=JS_GetException(ctx);
                assert(text_is(ctx,*output,"code","WIFI_RESTART_FAILED"));
                assert(text_is(ctx,*output,"operation","wifi.driver.restart"));
                *details=JS_GetPropertyStr(ctx,*output,"details");
                assert(JS_GetPropertyStr(ctx,*details,"lifecycleAdmitted")==((scenario>=3)?JS_TRUE:JS_FALSE));
                assert(JS_GetPropertyStr(ctx,*details,"checkpointAttempted")==((scenario>=4)?JS_TRUE:JS_FALSE));
                assert(JS_GetPropertyStr(ctx,*details,"cleanupPending")==((scenario>=3)?JS_TRUE:JS_FALSE));
                assert(JS_GetPropertyStr(ctx,*details,"restartRequired")==((scenario==5)?JS_TRUE:JS_FALSE));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*details,"configuration"))==(scenario<4 || scenario==7));
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"password")));
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"ssid")));
            }
        }
        if(JS_HasException(ctx))JS_GetException(ctx);
        /* VM result OOM after successful reconstruction does not undo or retry it. */
        if(driver_started)assert(execute_calls==1 && end_calls==1 && !s_wifi_configuration_cleanup);
        JS_PopGCRef(ctx,&details_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
