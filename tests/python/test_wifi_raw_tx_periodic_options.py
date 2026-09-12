"""Deferred real VM periodic capture with the production ByteSource helper."""
import re
import tempfile
import unittest

from test_wifi_rx_target import ROOT, INTERNAL, unit
from test_wifi_config_controls import structure
from wireless_vm_fixture import CORE, build, extract, run

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


class WiFiRawTxPeriodicOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        extra = unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_periodic.h')
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += BOUNDARY
        extra += extract((RAW / 'esp32_mquickjs_wifi_raw_tx.c').read_text(), 'esp32_mquickjs_wifi_raw_tx_capture_bytes')
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx_public_periodic.c').read_text()
        extra += extract(source, 'periodic_options')
        extra += 'typedef int esp_err_t;\n'
        extra += structure((INTERNAL / 'esp32_mquickjs_wifi_raw_tx_periodic_job.h').read_text(), 'esp32_mquickjs_wifi_raw_tx_periodic_job_status_t')
        extra += 'typedef esp32_mquickjs_wifi_raw_tx_periodic_job_status_t status_t;\n'
        extra += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        extra += extract(source, 'periodic_status_to_js')
        cls.binary = build(cls.temp.name, extra, MAIN)

    def test_required_interval_optional_bounds_bool_and_getter_ownership(self):
        cases = [
            ('({frame:view,intervalUs:1000})', True),
            ('({frame:view,intervalUs:4294967295,count:4294967295,startDelayUs:4294967295,busyPolicy:"stop",stopOnError:false,timeoutMs:60000})', True),
            ('({frame:view})', False), ('({frame:view,intervalUs:999})', False),
            ('({frame:view,intervalUs:1000.5})', False), ('({frame:view,intervalUs:4294967296})', False),
            ('({frame:view,intervalUs:1000,count:-1})', False),
            ('({frame:view,intervalUs:1000,count:4294967296})', False),
            ('({frame:view,intervalUs:1000,startDelayUs:-1})', False),
            ('({frame:view,intervalUs:1000,timeoutMs:0})', False),
            ('({frame:view,intervalUs:1000,timeoutMs:60001})', False),
            ('({frame:view,intervalUs:1000,busyPolicy:"skip\\u0000"})', False),
            ('({frame:view,intervalUs:1000,stopOnError:0})', False),
            ('({frame:view,intervalUs:1000,unknown:true})', False), ('null', False), ('[]', False),
            ('({intervalUs:1000,get frame(){throw new Error("sentinel");}})', False),
            ('({frame:view,get intervalUs(){throw new Error("sentinel");}})', False),
            ('({frame:view,get intervalUs(){view.close();return 1000;}})', False),
            ('(function(){var n=0,m=0;return {get frame(){if(++n!==1)throw new Error("twice");return view;},get intervalUs(){if(++m!==1)throw new Error("twice");return 1000;}};})()', True),
        ]
        for expression, valid in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), expression, str(int(valid)),
                     'sentinel' if 'sentinel' in expression else '', 'false' if 'stopOnError:false' in expression else 'true'])

    def test_status_converter_moving_gc_and_nth_allocation(self):
        run([str(self.binary), 'status', '1', '', 'true'])


BOUNDARY = r'''
#define ESP32_MQUICKJS_MEMORY_EXTERNAL 1
#undef esp32_mquickjs_memory_wireless_alloc
static void *esp32_mquickjs_memory_wireless_alloc(const char *tag,size_t length,int policy,int role) {
    assert(role==ESP32_MQUICKJS_MEMORY_BUDGET_TX);
    assert(!strcmp(tag,"wifi.raw-tx") && length>=24 && length<=1500 && policy==1);
    return heap_caps_malloc(length,1);
}
'''
MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[2]);
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,input_ref,error_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*input=JS_PushGCRef(ctx,&input_ref),*error=JS_PushGCRef(ctx,&error_ref);
        uint8_t *data=heap_caps_malloc(24,1);assert(data);memset(data,0,24);data[0]=0x80;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,data,24);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        bool convert=!strcmp(argv[1],"status");
        if(!convert){*input=JS_Eval(ctx,argv[1],strlen(argv[1]),"periodic",JS_EVAL_RETVAL);assert(!JS_IsException(*input));}
        esp32_mquickjs_wifi_raw_tx_periodic_options_t options={.interval_us=999};
        uint32_t timeout=0;uint8_t *bytes=NULL;size_t length=0;
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(convert) {
            status_t snapshot={.retired=true,.close_requested=true,.timer_quiesced=true,
                .ledger={.generation=55,.options={.interval_us=1000},.scheduled=5,.issued=2,.skipped_busy=2,.skipped_late=1}};
            *error=periodic_status_to_js(ctx,&snapshot);ok=!JS_IsException(*error);
        } else ok=periodic_options(ctx,&input_ref,&options,&timeout,&bytes,&length);
        inject=false;collect=false;
        if(!nth){total=calls;assert(ok==expected);}
        if(ok && convert) {
            assert(JS_GetPropertyStr(ctx,*error,"retired")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*error,"timerQuiesced")==JS_TRUE);
            JSValue generation=JS_GetPropertyStr(ctx,*error,"periodicGeneration");
            uint32_t n;assert(esp32_mquickjs_value_to_bounded_u32(ctx,generation,0,UINT32_MAX,&n) && n==55);
        } else if(ok) {
            assert(bytes && length==24 && bytes[0]==0x80 && options.interval_us>=1000);
            assert(timeout>=1 && timeout<=60000);
            assert(options.stop_on_error==(strcmp(argv[4],"false")!=0));
        } else {
            assert(JS_HasException(ctx) && options.interval_us==999);*error=JS_GetException(ctx);
            if(!nth && argv[3][0]) {
                *error=JS_GetPropertyStr(ctx,*error,"message");JSCStringBuf buffer;size_t size;
                const char *text=JS_ToCStringLen(ctx,&size,*error,&buffer);
                assert(text && size==strlen(argv[3]) && !memcmp(text,argv[3],size));
            }
        }
        if(bytes)heap_caps_free(bytes);
        JS_PopGCRef(ctx,&error_ref);JS_PopGCRef(ctx,&input_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
