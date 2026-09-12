"""Deferred real VM FTM option/status/report/error conversions with GC/Nth OOM.

Native Session allocation, close/release and report-entry read use production
functions. Prepared report bytes are the producer boundary; this does not replace
or execute Session/Radio scheduling, covered separately by the native fixtures.
"""
import re
import tempfile
import unittest
from test_wifi_config_controls import sdk_types
from test_wifi_rx_target import INTERNAL, unit
from wireless_vm_fixture import ROOT, CORE, build, extract, run

SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm.c'
SESSION = SOURCE.with_name('esp32_mquickjs_wifi_ftm_session.c')
RADIO = SOURCE.parent.parent / 'wifi_radio/esp32_mquickjs_wifi_radio.c'


class WiFiFtmCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text();native = SESSION.read_text()
        code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_FTM_ENABLE 1\n#define CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 0\n#define CONFIG_IDF_TARGET "esp32c5"\n'
        code += '#define JS_CLASS_WIFI_FTM_SESSION (JS_CLASS_USER+52)\n'
        code += 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 2\n#define ESP_ERR_INVALID_STATE 3\n#define ESP_ERR_NO_MEM 4\n#define ESP_ERR_TIMEOUT 5\n'
        code += sdk_types('esp32c5/representative', ('wifi_ftm_initiator_cfg_t','wifi_event_ftm_report_t','wifi_ftm_report_entry_t'))
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_ftm_radio.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_ftm_session.h')
        code += BOUNDARIES
        start = native.index('struct esp32_mquickjs_wifi_ftm_session {')
        end = native.index('/* Detach only', start)
        code += native[start:end]
        code += extract(RADIO.read_text(), 'esp32_mquickjs_wifi_ftm_config_valid')
        for name in ('ftm_detach_entries_locked','ftm_free_entries','esp32_mquickjs_wifi_ftm_retain',
                     'esp32_mquickjs_wifi_ftm_release','esp32_mquickjs_wifi_ftm_create','esp32_mquickjs_wifi_ftm_close',
                     'esp32_mquickjs_wifi_ftm_status','esp32_mquickjs_wifi_ftm_report_entry'):
            code += extract(native, name)
        code += re.search(r'typedef enum \{[^}]*\} ftm_operation_t;', source).group(0)
        code += re.search(r'typedef struct \{[^}]*\} ftm_options_t;', source).group(0)
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += source[start:source.index('\n};', start)+3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0)+'\n'
        for name in ('ftm_hex','ftm_mac','ftm_address','ftm_options','ftm_status_name','ftm_operation_name',
                     'ftm_error','ftm_status_to_js','ftm_timestamp','ftm_report_to_js','ftm_finish'):
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_capture_options_validate_before_allocation(self):
        base = 'peerAddress:"02:11:22:33:44:55",channel:6'
        cases = [('({'+base+'})',1),('({'+base+',frameCount:64,burstPeriodMs:10000,maxReportEntries:0,timeoutMs:1})',1),
                 ('({'+base+',burstPeriodMs:100})',0),('({'+base+',burstPeriodMs:201})',0),
                 ('({'+base+',frameCount:8})',0),('({'+base+',frameCount:32.1})',0),
                 ('({'+base+',maxReportEntries:65})',0),('({'+base+',maxReportEntries:4294967295})',0),
                 ('({'+base+',timeoutMs:0})',0),('({'+base+',timeoutMs:"10"})',0),
                 ('({'+base+',extra:true})',0),('({'+base.replace('02:','03:')+'})',0),
                 ('({'+base.replace('02:11:22:33:44:55','00:00:00:00:00:00')+'})',0),
                 ('({'+base.replace('44:55','44:55\\u0000')+'})',0),
                 ('({'+base.replace('channel:6','channel:36')+'})',0),('null',0),('[]',0),
                 ('({'+base+',get frameCount(){gc();throw new Error("sentinel");}})',0),
                 ('(function(){var n=0;return {'+base+',get frameCount(){gc();if(++n!==1)throw new Error("twice");return 24;}};})()',1)]
        for value,expected in cases:
            with self.subTest(value=value):run([str(self.binary),'capture',value,str(expected)])

    def test_report_words_gc_each_allocation_and_retry(self):
        for mode in ('report','status','error','receive-timeout'):run([str(self.binary),mode,'','1'])


BOUNDARIES = r'''
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool session_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!session_locked);session_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(session_locked);session_locked=false;}while(0)
static const char *esp_err_to_name(int error){(void)error;return "injected";}
'''
MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref,result_ref,array_ref,item_ref,word_ref;
        JSValue *root=JS_PushGCRef(ctx,&root_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JSValue *array=JS_PushGCRef(ctx,&array_ref),*item=JS_PushGCRef(ctx,&item_ref),*word=JS_PushGCRef(ctx,&word_ref);
        esp32_mquickjs_wifi_ftm_session_t *session=NULL;
        esp32_mquickjs_wifi_ftm_status_t status={0};
        if(!strcmp(argv[1],"capture")) {
            *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"ftm-capture",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        } else {
            wifi_ftm_initiator_cfg_t config={.resp_mac={2,3,4,5,6,7},.channel=6};
            assert(!esp32_mquickjs_wifi_ftm_create(&config,2,&session));
            session->status.started=session->status.submitted=session->status.retired=session->status.report_ready=true;
            session->status.native=(esp32_mquickjs_wifi_ftm_state_t){.token={7,45},.submitted=true,.terminal=true,
                .report_consumed=true,.copied_entries=2,.sdk_fenced=true,.event_fenced=true,
                .report={.peer_mac={2,3,4,5,6,7},.status=FTM_STATUS_SUCCESS,.rtt_raw=UINT32_MAX,.rtt_est=20,.dist_est=30,.ftm_report_num_entries=3}};
            session->entries[0]=(wifi_ftm_report_entry_t){.dlog_token=255,.rssi=-127,.rtt=UINT32_MAX,.ppm=-32768,
                .t1=UINT64_MAX,.t2=UINT64_C(0x123456789abcdef0),.t3=UINT64_C(0x100000000),.t4=0};
            session->entries[1]=session->entries[0];assert(esp32_mquickjs_wifi_ftm_status(session,&status));
        }
        calls=0;fail_at=nth;inject=true;collect=true;bool ok;
        if(!strcmp(argv[1],"capture")) {
            ftm_options_t options={0};ok=ftm_options(ctx,&root_ref,&options);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok)assert(esp32_mquickjs_wifi_ftm_config_valid(&options.config) && options.report_capacity<=64);
            else assert(JS_HasException(ctx));
        } else if(!strcmp(argv[1],"report")) {
            *result=ftm_report_to_js(ctx,session,&status);ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
        } else if(!strcmp(argv[1],"status")) {
            *result=ftm_status_to_js(ctx,&status);ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok);}
        } else if(!strcmp(argv[1],"receive-timeout")) {
            esp32_mquickjs_future_driver_state_t state={.operation=FTM_RECEIVE,.receive_timed_out=true};
            *result=ftm_finish(ctx,&state);ok=*result==JS_NULL;assert(ok);if(!nth)total=calls;
        } else {
            esp32_mquickjs_future_driver_state_t state={.session=session,.operation=FTM_END};
            *result=ftm_error(ctx,&state,true);ok=!JS_IsException(*result);assert(!ok && JS_HasException(ctx));
            if(!nth)total=calls;
        }
        inject=collect=false;
        if(JS_HasException(ctx))(void)JS_GetException(ctx);
        if(session && !strcmp(argv[1],"report")) {
            assert(session->status.report_ready && session->entries[0].t1==UINT64_MAX);
            if(!ok){*result=ftm_report_to_js(ctx,session,&status);assert(!JS_IsException(*result));}
            *array=JS_GetPropertyStr(ctx,*result,"entries");*item=JS_GetPropertyUint32(ctx,*array,0);
            *word=JS_GetPropertyStr(ctx,*item,"t2Ps");
            double low,high;assert(!JS_ToNumber(ctx,&low,JS_GetPropertyStr(ctx,*word,"low")));
            assert(!JS_ToNumber(ctx,&high,JS_GetPropertyStr(ctx,*word,"high")));
            assert(low==0x9abcdef0U && high==0x12345678U);
            assert(JS_GetPropertyStr(ctx,*result,"truncated")==JS_TRUE);
        }
        if(session){esp32_mquickjs_wifi_ftm_close(session);esp32_mquickjs_wifi_ftm_release(session);}
        assert(!s_ftm_handles && !s_ftm_reserved_entries && !native_live && !session_locked);
        JS_PopGCRef(ctx,&word_ref);JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&array_ref);
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&root_ref);
        JS_FreeContext(ctx);free(heap);assert(!root_count);
    }
    return 0;
}
'''
