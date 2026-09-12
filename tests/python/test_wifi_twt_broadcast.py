"""Deferred production broadcast discovery Radio/Future and real VM conversion.

SDK read and background-worker scheduling are injected boundaries. Native
discovery/dispatch are exercised in test_wifi_twt_sdk. This does not replace
Future-core teardown or actual native-task/RF evidence. AST only this wave.
"""
import re
import tempfile
import unittest
from test_wifi_twt_radio import radio_code, MAIN as RADIO_MAIN, SOURCE as RADIO_SOURCE, INTERNAL
from test_wifi_driver_phy import COMPONENT
from wireless_vm_fixture import CORE, build, extract, run


def discovery_code():
    source = (COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_broadcast.c').read_text()
    code = radio_code(True) + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
    code += '#define ESP_ERR_TIMEOUT 99\n#define ESP32_MQUICKJS_WIFI_TWT_DEFAULT_TIMEOUT_MS 6000U\n'
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
    code += BOUNDARIES
    code += extract(RADIO_SOURCE.read_text(), 'esp32_mquickjs_wifi_radio_twt_broadcast_snapshot')
    code += source[source.index('struct esp32_mquickjs_future_driver_state {'):source.index('#define SET')]
    code += (CORE / 'esp32_mquickjs_options.c').read_text()
    code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include "')) + '\n'
    code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
    code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
    for name in ('broadcast_error', 'broadcast_capture', 'broadcast_worker', 'broadcast_schedule',
                 'broadcast_poll', 'broadcast_finish', 'broadcast_cancel', 'broadcast_timeout'):
        code += extract(source, name)
    # One-line release function is copied separately (extract expects multiline bodies).
    code += re.search(r'^static void broadcast_destroy\(.*$', source, re.M).group(0) + '\n'
    return code


class WiFiTwtBroadcast(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = build(cls.temp.name, discovery_code(), MAIN)

    def test_capture_gc_and_nth_allocation_failure(self):
        cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                 ('({timeoutMs:1})', 1), ('({timeoutMs:60000})', 1), ('({timeoutMs:60001})', 0),
                 ('({timeoutMs:0})', 0), ('({timeoutMs:1.5})', 0), ('({timeoutMs:"2"})', 0),
                 ('({timeoutMs:NaN})', 0), ('({timeoutMs:Infinity})', 0), ('({extra:1})', 0),
                 ('({get timeoutMs(){gc();return 12;}})', 1),
                 ('({get timeoutMs(){gc();throw 12345;}})', 0)]
        for value, valid in cases:
            with self.subTest(value=value):
                run([str(self.binary), 'capture', value, str(valid)])

    def test_snapshot_conversion_maximum_interval_and_allocation_failure(self):
        for mode in ('result', 'error'):
            run([str(self.binary), mode, 'undefined', '1'])

    def test_worker_saturation_cancel_timeout_and_radio_admission(self):
        run([str(self.binary), 'lifecycle', 'undefined', '1'])


BOUNDARIES = r'''
static void (*worker)(void *);
static void *worker_arg;
static bool worker_full;
static unsigned reads;
static int read_error;
static esp32_mquickjs_wifi_twt_broadcast_snapshot_t discovered;
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(esp32_mquickjs_wifi_twt_broadcast_snapshot_t *out) {
    assert(locks && !critical);++reads;if(read_error)return read_error;*out=discovered;return 0;
}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!critical);if(worker_full || worker)return false;worker=fn;worker_arg=arg;return true;
}
static void work(void) {
    assert(worker);void (*fn)(void *)=worker;void *arg=worker_arg;worker=NULL;worker_arg=NULL;fn(arg);
}
static const char *esp_err_to_name(int error) {(void)error;return "injected";}
'''
MAIN = r'''
static esp32_mquickjs_future_driver_state_t *capture(JSContext *ctx) {
    esp32_mquickjs_future_driver_state_t *s=NULL;assert(broadcast_capture(ctx,NULL,0,NULL,&s));return s;
}
static void discovery_lifecycle(JSContext *ctx) {
    reset_twt();esp32_mquickjs_future_driver_state_t *s=capture(ctx);
    worker_full=true;assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && !worker);
    assert(broadcast_cancel(s)==ESP32_MQUICKJS_CANCEL_REQUESTED);
    assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !reads);broadcast_destroy(s);
    worker_full=false;s=capture(ctx);broadcast_schedule(s);assert(worker);
    assert(JS_IsException(broadcast_timeout(ctx,s,6000)));(void)JS_GetException(ctx);
    assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_PENDING && native_live==1);
    work();assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_READY && !reads);broadcast_destroy(s);
    s=capture(ctx);broadcast_schedule(s);work();assert(reads==1 && !s->error);
    assert(broadcast_poll(s)==ESP32_MQUICKJS_FUTURE_READY);
    assert(s->generation==s_radio.generation && !s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT]);
    broadcast_destroy(s);
    s_radio.lifecycle.identity=1;s=capture(ctx);broadcast_schedule(s);work();
    assert(s->error==ESP_ERR_INVALID_STATE && reads==1);broadcast_destroy(s);s_radio.lifecycle.identity=0;
    read_error=79;s=capture(ctx);broadcast_schedule(s);work();
    assert(s->error==79 && reads==2 && !s->generation);broadcast_destroy(s);
    assert(!locks && !critical && !worker && !native_live);
}
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);bool input=!strcmp(argv[1],"capture");
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        if(!strcmp(argv[1],"lifecycle")){discovery_lifecycle(ctx);JS_FreeContext(ctx);free(heap);assert(!root_count);return 0;}
        reset_twt();JSGCRef ref,outref;JSValue *value=JS_PushGCRef(ctx,&ref),*out=JS_PushGCRef(ctx,&outref);
        *value=JS_Eval(ctx,argv[2],strlen(argv[2]),"broadcast-discovery",JS_EVAL_RETVAL);assert(!JS_IsException(*value));
        esp32_mquickjs_future_driver_state_t *s=NULL,complete={.generation=9};
        complete.snapshot.count=32;complete.snapshot.joined_bitmap=UINT32_C(0x80000001);
        for(unsigned i=0;i<32;++i)complete.snapshot.schedules[i]=(esp_wifi_btwt_info_t){.btwt_id_in_use=true,
            .btwt_info_id=i,.btwt_trigger=1,.btwt_wake_duration=255,.btwt_wake_interval_mantissa=65535,
            .btwt_wake_interval_exponent=31,.btwt_info_persistence=255};
        bool error_mode=!strcmp(argv[1],"error");if(error_mode)complete.error=71;
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=input?broadcast_capture(ctx,NULL,1,&ref,&s):!JS_IsException(*out=broadcast_finish(ctx,&complete));
        inject=collect=false;
        if(!nth){total=calls;assert(ok==(error_mode?false:expected));}else assert(!ok);
        if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        if(!input && ok) {
            *value=JS_GetPropertyStr(ctx,*out,"schedules");*value=JS_GetPropertyUint32(ctx,*value,31);
            *value=JS_GetPropertyStr(ctx,*value,"wakeIntervalUs");double interval;
            assert(!JS_ToNumber(ctx,&interval,*value) && interval==140735340871680.0);
        }
        if(s)broadcast_destroy(s);
        JS_PopGCRef(ctx,&outref);JS_PopGCRef(ctx,&ref);assert(!native_live && !root_count);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
'''
