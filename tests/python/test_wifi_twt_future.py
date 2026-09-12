"""Deferred production probe capture/conversion/worker callbacks with real VM.

Uses real Radio lease glue. Only worker scheduling and SDK/retirement boundaries
are controlled. Direct callbacks do not replace full Future-core scheduler or
ESP32 GC/RF qualification. Do not import, compile or execute this wave.
"""
import re
import tempfile
import unittest
from test_wifi_twt_radio import radio_code, MAIN as RADIO_MAIN, INTERNAL
from test_wifi_driver_phy import COMPONENT
from test_wifi_config_controls import structure
from wireless_vm_fixture import CORE, build, extract, run

SOURCE = COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt.c'


def future_code():
    source = SOURCE.read_text()
    code = radio_code(True) + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
    code += '#define ESP_ERR_TIMEOUT 99\n#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_RESPONSE_MS 5000U\n#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS 6000U\n'
    information = (INTERNAL / 'esp32_mquickjs_wifi_twt_information.h').read_text()
    code += re.search(r'^#define ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS.*$', information, re.M).group(0) + '\n'
    code += structure(information, 'esp32_mquickjs_wifi_twt_information_result_t')
    code += '''static bool esp32_mquickjs_wifi_twt_information_snapshot(esp32_mquickjs_wifi_twt_information_result_t *state) {
        *state=(esp32_mquickjs_wifi_twt_information_result_t){.identity=4,.setup_identity=8,.duration_ms=10,.complete=true,.tx_complete=true};return true;
    }\n'''
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
    code += 'bool esp32_mquickjs_wifi_twt_agreement_service(void) {return false;}\nbool esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy(void) {return true;}\n'
    code += BOUNDARIES
    start = source.index('struct esp32_mquickjs_future_driver_state {')
    code += source[start:source.index('#define SET', start)]
    code += (CORE / 'esp32_mquickjs_options.c').read_text().replace('#include "esp32_mquickjs_options.h"', '')
    # Options may gain includes: use the same include-stripping helper as other VM fixtures.
    code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include "')) + '\n'
    code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
    code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
    code += extract(source, 'twt_information_value')
    for name in ('twt_probe_status_name', 'twt_error', 'twt_capture', 'twt_submit_worker', 'twt_schedule',
                 'twt_poll', 'twt_finish', 'twt_cancel', 'twt_destroy', 'twt_on_timeout',
                 'js_wifi_twt_status', 'js_wifi_twt_capabilities'):
        code += extract(source, name)
    return code


class WiFiTwtFuture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = build(cls.temp.name, future_code(), MAIN)

    def test_capture_and_conversion_nth_failure_rooting(self):
        cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                 ('({responseTimeoutMs:1,timeoutMs:60000})', 1),
                 ('({responseTimeoutMs:0})', 0), ('({responseTimeoutMs:60001})', 0),
                 ('({timeoutMs:0})', 0), ('({timeoutMs:1.5})', 0), ('({timeoutMs:"2"})', 0),
                 ('({extra:1})', 0), ('({timeoutMs:NaN})', 0), ('({timeoutMs:Infinity})', 0),
                 ('({get responseTimeoutMs(){gc();return 4;},get timeoutMs(){gc();return 8;}})', 1),
                 ('({get timeoutMs(){gc();throw 12345;}})', 0)]
        for value, valid in cases:
            with self.subTest(value=value):
                run([str(self.binary), 'capture', value, str(valid)])
        for mode in ('result', 'error', 'status', 'capabilities'):
            run([str(self.binary), mode, 'undefined', '1'])

    def test_cancel_publication_queue_saturation_cleanup_and_runtime_gate(self):
        run([str(self.binary), 'lifecycle', 'undefined', '1'])


BOUNDARIES = r'''
#define CONFIG_IDF_TARGET "esp32c5"
static int64_t clock_us;
static void (*worker)(void *);
static void *worker_arg;
static bool worker_full;
static int64_t esp_timer_get_time(void) {return clock_us;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!critical);if(worker_full || worker)return false;worker=fn;worker_arg=arg;return true;
}
static void run_worker(void) {
    assert(worker);void (*fn)(void *)=worker;void *arg=worker_arg;worker=NULL;worker_arg=NULL;fn(arg);
}
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool retired_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!retired_locked);retired_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(retired_locked);retired_locked=false;}while(0)
static const char *esp_err_to_name(int error) {(void)error;return "injected";}
static JSValue twt_get_property(JSContext *ctx,JSValue object,const char *name) {
    JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);*root=object;
    JSValue value=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,name);
    JS_PopGCRef(ctx,&ref);return value;
}
#define JS_GetPropertyStr twt_get_property
'''
MAIN = r'''
static esp32_mquickjs_future_driver_state_t *capture(JSContext *ctx) {
    esp32_mquickjs_future_driver_state_t *s=NULL;assert(twt_capture(ctx,NULL,0,NULL,&s));return s;
}
static void probe_lifecycle(JSContext *ctx) {
    reset_twt();esp32_mquickjs_future_driver_state_t *s=capture(ctx);
    twt_schedule(s);assert(worker && s->submitted && !submits);
    assert(twt_cancel(s)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && !atomic_load(&s->worker_done));
    JSValue error=twt_on_timeout(ctx,s,6000);assert(JS_IsException(error));(void)JS_GetException(ctx);
    run_worker();assert(!submits && twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY);
    twt_destroy(s);assert(!native_live && !twt_retired_pending() && !worker);

    s=capture(ctx);early=true;twt_schedule(s);run_worker();
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !s->error && s->result.native.event_seen);
    native_result.event.status=ITWT_PROBE_TIMEOUT;native_result.event.reason=18;
    native_result.observation_error=88;
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !s->error);
    JSGCRef ref;JSValue *value=JS_PushGCRef(ctx,&ref);*value=twt_finish(ctx,s);assert(!JS_IsException(*value));
    *value=JS_GetPropertyStr(ctx,*value,"status");
    JSCStringBuf buf;assert(!strcmp(JS_ToCString(ctx,*value,&buf),"timeout"));
    JS_PopGCRef(ctx,&ref);
    uint64_t owner=s->token.identity;
    worker_full=true;twt_destroy(s);
    assert(!native_live && twt_retired_pending() && s_twt_retired.token.identity==owner && !worker);
    assert(!esp32_mquickjs_prepare_wifi_twt_runtime_destroy());
    esp32_mquickjs_future_driver_state_t *next=capture(ctx);twt_schedule(next);assert(!next->submitted);
    worker_full=false;clock_us+=100000;retire_error=76;
    assert(esp32_mquickjs_wifi_twt_service() && worker);run_worker();
    assert(twt_retired_pending() && s_twt_retired.error==76 && s_twt_retired.token.identity==owner);
    twt_schedule(next);assert(!next->submitted && !worker);
    retire_error=0;clock_us+=100000;
    assert(!esp32_mquickjs_prepare_wifi_twt_runtime_destroy() && worker);run_worker();
    assert(esp32_mquickjs_prepare_wifi_twt_runtime_destroy() && !s_radio.operation.identity);
    twt_schedule(next);assert(next->submitted && worker);
    assert(twt_cancel(next)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(twt_poll(next)==ESP32_MQUICKJS_FUTURE_PENDING);run_worker();
    assert(twt_poll(next)==ESP32_MQUICKJS_FUTURE_READY);twt_destroy(next);
    assert(!native_live && !worker && !retired_locked && !locks && !critical);
    /* Recovery asks Radio to close while the original public Future lives.
     * Late destruction must not overwrite a successor's native cleanup slot. */
    s=capture(ctx);twt_schedule(s);run_worker();
    assert(esp32_mquickjs_wifi_radio_twt_probe_request_close(&s->token));
    assert(twt_poll(s)==ESP32_MQUICKJS_FUTURE_READY && s->error==ESP_ERR_INVALID_STATE);
    clock_us+=100000;assert(esp32_mquickjs_wifi_twt_service());run_worker();
    assert(!twt_retired_pending() && !s_radio.operation.identity);
    next=capture(ctx);twt_schedule(next);run_worker();
    assert(esp32_mquickjs_wifi_radio_twt_probe_request_close(&next->token));
    worker_full=true;(void)esp32_mquickjs_wifi_twt_service();
    uint64_t successor=s_twt_retired.token.identity;
    twt_destroy(s);assert(s_twt_retired.token.identity==successor && successor==next->token.identity);
    worker_full=false;clock_us+=100000;assert(esp32_mquickjs_wifi_twt_service());run_worker();
    twt_destroy(next);assert(!native_live && !twt_retired_pending());
}
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);
    bool input=!strcmp(argv[1],"capture");
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        if(!strcmp(argv[1],"lifecycle")) {probe_lifecycle(ctx);JS_FreeContext(ctx);free(heap);assert(!root_count);return 0;}
        reset_twt();JSGCRef ref,outref;JSValue *value=JS_PushGCRef(ctx,&ref),*out=JS_PushGCRef(ctx,&outref);
        *value=JS_Eval(ctx,argv[2],strlen(argv[2]),"twt-public",JS_EVAL_RETVAL);assert(!JS_IsException(*value));
        esp32_mquickjs_future_driver_state_t *s=NULL;
        esp32_mquickjs_future_driver_state_t complete={.token={7,9},.result={.native_identity=31,
            .native={.event_seen=true,.event={.status=ITWT_PROBE_SUCCESS,.reason=0}}}};
        if(!strcmp(argv[1],"error")) {complete.error=81;complete.stage="submit";}
        calls=0;fail_at=nth;inject=collect=true;bool ok;
        if(input)ok=twt_capture(ctx,NULL,1,&ref,&s);
        else {
            if(!strcmp(argv[1],"status"))*out=js_wifi_twt_status(ctx,NULL,0,NULL);
            else if(!strcmp(argv[1],"capabilities"))*out=js_wifi_twt_capabilities(ctx,NULL,0,NULL);
            else *out=twt_finish(ctx,&complete);
            ok=!JS_IsException(*out);
        }
        inject=collect=false;
        bool error_mode=!strcmp(argv[1],"error");
        if(!nth){total=calls;assert(ok==(error_mode?false:expected));}else assert(!ok);
        if(!ok) {
            assert(JS_HasException(ctx));JSValue exception=JS_GetException(ctx);
            if(!nth && strstr(argv[2],"throw 12345"))assert(exception==JS_NewInt32(ctx,12345));
        }
        if(s){assert(s->response_ms>=1 && s->response_ms<=60000 && !s->token.identity);twt_destroy(s);}
        assert(!native_live && !submits && !twt_retired_pending());
        JS_PopGCRef(ctx,&outref);JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);assert(!root_count);
    }
    return 0;
}
'''
