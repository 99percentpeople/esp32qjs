"""Deferred production Neighbor Report options/conversion with real VM GC/OOM.

The report producer is injected; allocation, ownership, chunk reads and decoded
fields are production code. Does not stand in for SDK/Radio/Future scheduling.
"""
import re
import tempfile
import unittest
from test_wifi_rx_target import INTERNAL, unit
from test_wifi_config_controls import structure
from wireless_vm_fixture import ROOT, CORE, build, extract, run

MODULE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_roaming'


class WiFiNeighborCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.temp.cleanup)
        source = (MODULE / 'esp32_mquickjs_wifi_neighbor_request.c').read_text()
        native = (MODULE / 'esp32_mquickjs_wifi_rrm_request.c').read_text()
        radio = (INTERNAL / 'esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_RRM_SUPPORT 1\n'
        code += 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 2\n#define ESP_ERR_INVALID_STATE 3\n#define ESP_ERR_NO_MEM 4\n#define ESP_ERR_TIMEOUT 5\n#define ESP32_MQUICKJS_WIFI_WATCH_MAX_NEIGHBORS 64U\n'
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', radio).group(0)
        code += structure(radio, 'esp32_mquickjs_wifi_radio_operation_t')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_rrm_sdk.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_rrm_request.h')
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_neighbor.h')
        code += BOUNDARIES
        code += native[native.index('struct esp32_mquickjs_wifi_rrm_request {'):native.index('static void rrm_free_report')]
        for name in ['rrm_free_report', 'rrm_detach_report_locked', 'esp32_mquickjs_wifi_rrm_retain',
                     'esp32_mquickjs_wifi_rrm_release', 'esp32_mquickjs_wifi_rrm_create', 'rrm_cancel_locked',
                     'esp32_mquickjs_wifi_rrm_close', 'esp32_mquickjs_wifi_rrm_status', 'esp32_mquickjs_wifi_rrm_copy']:
            code += extract(native, name)
        code += unit(MODULE.parent / 'wifi_common/esp32_mquickjs_wifi_neighbor.c')
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        for name in ['neighbor_terminal', 'neighbor_snapshot', 'neighbor_error', 'neighbor_options', 'neighbor_report']:
            code += extract(source, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_options_getters_and_report_gc_allocation_failures(self):
        for value, valid in [('({})', 1), ('undefined', 1), ('null', 0), ('[]', 0),
                             ('({maxReportBytes:4096,timeoutMs:60000})', 1),
                             ('({maxReportBytes:0})', 0), ('({maxReportBytes:4097})', 0),
                             ('({maxReportBytes:1.5})', 0), ('({maxReportBytes:"10"})', 0),
                             ('({timeoutMs:0})', 0), ('({timeoutMs:60001})', 0), ('({extra:1})', 0),
                             ('({get timeoutMs(){gc();return 2;},get maxReportBytes(){gc();return 32;}})', 1),
                             ('({get maxReportBytes(){gc();throw new Error("sentinel");}})', 0)]:
            with self.subTest(value=value):run([str(self.binary), 'capture', value, str(valid)])
        for mode in ['report', 'status', 'truncated', 'duplicate-preference', 'too-many']:
            run([str(self.binary), mode, '', '1'])


BOUNDARIES = r'''
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool request_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!request_locked);request_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(request_locked);request_locked=false;}while(0)
'''
MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[3]);
    bool capture=!strcmp(argv[1],"capture"),report=!strcmp(argv[1],"report"),status_mode=!strcmp(argv[1],"status");
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref,result_ref,list_ref,item_ref;
        JSValue *root=JS_PushGCRef(ctx,&root_ref),*result=JS_PushGCRef(ctx,&result_ref);
        JSValue *list=JS_PushGCRef(ctx,&list_ref),*item=JS_PushGCRef(ctx,&item_ref);
        esp32_mquickjs_wifi_rrm_request_t *request=NULL;
        if(capture){*root=JS_Eval(ctx,argv[2],strlen(argv[2]),"neighbor-options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));}
        else {
            assert(esp32_mquickjs_wifi_rrm_create(4096,&request)==ESP_OK);
            static const uint8_t bytes[]={77,52,16,2,0,0,0,0,1,255,255,255,255,81,6,9,3,1,255};
            memcpy(request->report,bytes,sizeof(bytes));
            request->status.started=request->status.retired=true;
            request->status.terminal=ESP32_MQUICKJS_WIFI_RRM_REPORT;
            request->status.received_bytes=sizeof(bytes);
            request->status.operation=(esp32_mquickjs_wifi_radio_operation_t){.identity=7,.generation=9};
            if(!strcmp(argv[1],"truncated"))request->status.received_bytes--;
            if(!strcmp(argv[1],"duplicate-preference")) {
                request->report[2]+=3;request->report[19]=3;request->report[20]=1;request->report[21]=0;
                request->status.received_bytes+=3;
            }
            if(!strcmp(argv[1],"too-many")) {
                size_t n=1;for(unsigned i=0;i<65;i++){request->report[n++]=52;request->report[n++]=13;memcpy(request->report+n,bytes+3,13);n+=13;}
                request->status.received_bytes=n;
            }
        }
        esp32_mquickjs_wifi_rrm_status_t status={0};
        if(request)assert(esp32_mquickjs_wifi_rrm_status(request,&status));
        esp32_mquickjs_future_driver_state_t state={.request=request};
        calls=0;fail_at=nth;inject=collect=true;bool ok;
        if(capture) {
            uint32_t timeout,capacity;ok=neighbor_options(ctx,1,&root_ref,true,&timeout,&capacity);
            if(!nth){total=calls;assert(ok==expected);}
            if(ok)assert(timeout>=1 && timeout<=60000 && capacity>=1 && capacity<=4096);
        } else {
            *result=status_mode ? neighbor_snapshot(ctx,&status) : neighbor_report(ctx,&state,&status);
            ok=!JS_IsException(*result);
            if(!nth){total=calls;assert(ok==(report||status_mode));}
        }
        inject=collect=false;
        if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        if(report) {
            if(!ok){*result=neighbor_report(ctx,&state,&status);assert(!JS_IsException(*result));}
            *list=JS_GetPropertyStr(ctx,*result,"neighbors");*item=JS_GetPropertyUint32(ctx,*list,0);
            double information;assert(!JS_ToNumber(ctx,&information,JS_GetPropertyStr(ctx,*item,"bssidInformation")));
            assert(information==4294967295.0 && request->report[0]==77 && request->status.retired);
        }
        if(request){esp32_mquickjs_wifi_rrm_close(request);esp32_mquickjs_wifi_rrm_release(request);}
        assert(!s_rrm_handles && !s_rrm_reserved_bytes && !native_live && !request_locked);
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&list_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&root_ref);
        JS_FreeContext(ctx);free(heap);assert(!root_count);
    }
    return 0;
}
'''
