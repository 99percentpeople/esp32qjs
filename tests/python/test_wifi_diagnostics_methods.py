"""Production driver dump admission and MQuickJS diagnostics GC/OOM; deferred."""

from pathlib import Path
import re
import tempfile
import unittest

from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT, CORE, build, extract, run

BASE = ROOT / "components/esp32_mquickjs"


def native_code():
    header = (BASE / "internal/esp32_mquickjs_wifi_radio.h").read_text()
    state = re.search(r"typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_driver_state_t;", header).group(0)
    radio = (BASE / "src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c").read_text()
    return "#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n" + state + BOUNDARIES + extract(radio, "esp32_mquickjs_wifi_radio_dump_stats") + RESET


class WiFiDiagnosticsMethods(unittest.TestCase):
    def test_native_mask_admission_mutex_and_original_sdk_error(self):
        compile_run(self, native_code() + NATIVE_MAIN)

    def test_real_public_dump_validation_and_error_conversion_gc_oom(self):
        source = (BASE / "src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c").read_text()
        code = native_code() + extract((CORE / "esp32_mquickjs.c").read_text(), "esp32_mquickjs_throw_native_error")
        for name in ("wifi_diagnostics_capture_mask", "js_wifi_diagnostics_dump_driver_stats"):
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, DUMP_MAIN)
            for scenario in ("success", "sdk-error", "admission", "invalid"):
                run([str(binary), scenario])

    def test_actual_generated_coverage_conversion_survives_gc_and_nth_failure(self):
        source = (BASE / "src/modules/wifi/esp32_mquickjs_wifi_diagnostics.c").read_text()
        code = COVERAGE_BOUNDARY + (BASE / "internal/esp32_mquickjs_wifi_coverage.inc").read_text()
        for name in ("wifi_coverage_counts_to_js", "wifi_coverage_rows_to_js", "js_wifi_diagnostics_idf_api_coverage"):
            code += extract(source, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, COVERAGE_MAIN)
            run([str(binary)])


BOUNDARIES = r'''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_WIFI_NOT_INIT 3
#define WIFI_STATIS_BUFFER 1U
#define WIFI_STATIS_RXTX 2U
#define WIFI_STATIS_HW 4U
#define WIFI_STATIS_DIAG 8U
#define WIFI_STATIS_PS 16U
#define WIFI_STATIS_ALL (-1)
static unsigned locks, sdk_calls, locked_calls;
static uint32_t seen_mask;
static int sdk_error;
static struct {
    bool driver_owned, storage_configured, restart_required;
    struct {unsigned identity;} lifecycle, operation;
    const char *fault_stage, *cleanup_stage;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
} s_radio;
static void wifi_radio_operation_lock(void) {assert(!locks);locks++;locked_calls++;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);locks--;}
static int esp_wifi_statis_dump(uint32_t mask) {assert(locks==1);sdk_calls++;seen_mask=mask;return sdk_error;}
'''
RESET = r'''
static void reset(void) {
    assert(!locks);memset(&s_radio,0,sizeof(s_radio));
    s_radio.driver_owned=s_radio.storage_configured=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    sdk_calls=locked_calls=0;sdk_error=0;seen_mask=0;
}
'''
NATIVE_MAIN = r'''
int main(void) {
    const char *stage;
    reset();
    for(uint32_t mask=0;mask<=31;mask++) {
        assert(esp32_mquickjs_wifi_radio_dump_stats(mask,&stage)==ESP_OK && !stage && seen_mask==mask);
    }
    assert(sdk_calls==32 && locked_calls==32);
    assert(esp32_mquickjs_wifi_radio_dump_stats(UINT32_MAX,&stage)==ESP_OK && seen_mask==UINT32_MAX);
    unsigned count=sdk_calls,entered=locked_calls;
    assert(esp32_mquickjs_wifi_radio_dump_stats(32,&stage)==ESP_ERR_INVALID_ARG && !strcmp(stage,"mask"));
    assert(esp32_mquickjs_wifi_radio_dump_stats(0,NULL)==ESP_ERR_INVALID_ARG);
    assert(sdk_calls==count && locked_calls==entered);
    for(unsigned scenario=0;scenario<8;scenario++) {
        reset();
        switch(scenario) {
        case 0:s_radio.driver_owned=false;break;
        case 1:s_radio.storage_configured=false;break;
        case 2:s_radio.lifecycle.identity=7;break;
        case 3:s_radio.operation.identity=9;break;
        case 4:s_radio.fault_stage="failed";break;
        case 5:s_radio.cleanup_stage="pending";break;
        case 6:s_radio.restart_required=true;break;
        case 7:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;break;
        }
        assert(esp32_mquickjs_wifi_radio_dump_stats(31,&stage)!=ESP_OK && !strcmp(stage,"admission") && !sdk_calls && !locks);
    }
    reset();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;sdk_error=91;
    assert(esp32_mquickjs_wifi_radio_dump_stats(3,&stage)==91 && !strcmp(stage,"driver-dump") && sdk_calls==1 && !locks);
    assert(!s_radio.fault_stage && !s_radio.restart_required);
}
'''
DUMP_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==2);int total=0;bool invalid=!strcmp(argv[1],"invalid");
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        reset();if(!strcmp(argv[1],"sdk-error"))sdk_error=91;
        if(!strcmp(argv[1],"admission"))s_radio.lifecycle.identity=3;
        JSGCRef rr,ar;JSValue *r=JS_PushGCRef(ctx,&rr),*a=JS_PushGCRef(ctx,&ar);*a=JS_UNDEFINED;
        if(invalid) {
            const char *expressions[]={"true","null","'1'","({valueOf:function(){throw Error('coercion');}})","NaN","Infinity","0.5","-2","32","4294967296"};
            for(unsigned i=0;i<sizeof(expressions)/sizeof(*expressions);i++) {
                *a=JS_Eval(ctx,expressions[i],strlen(expressions[i]),"mask.js",JS_EVAL_RETVAL);assert(!JS_IsException(*a));
                *r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,1,a);assert(JS_IsException(*r)&&!sdk_calls&&!locked_calls);JS_GetException(ctx);
            }
            *r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,2,NULL);assert(JS_IsException(*r)&&!locked_calls);JS_GetException(ctx);
        } else {
            inject=true;collect=true;calls=0;fail_at=nth;
            *r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,0,NULL);
            inject=false;collect=false;if(!nth)total=calls;
            assert(sdk_calls==(!s_radio.lifecycle.identity));
            if(!nth)assert(JS_IsException(*r)==(sdk_error!=0||s_radio.lifecycle.identity!=0));
            if(JS_IsException(*r)) {
                assert(JS_HasException(ctx));*r=JS_GetException(ctx);
                if(!nth) {
                    *a=JS_GetPropertyStr(ctx,*r,"details");uint32_t code;
                    assert(!JS_ToUint32(ctx,&code,JS_GetPropertyStr(ctx,*a,"espCode"))&&code==(sdk_error?sdk_error:ESP_ERR_INVALID_STATE));
                    JSCStringBuf b;const char *stage=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*a,"stage"),&b);
                    assert(stage&&!strcmp(stage,sdk_error?"driver-dump":"admission"));
                }
            }
            else assert(*r==JS_TRUE && seen_mask==UINT32_MAX && !sdk_error && !s_radio.lifecycle.identity);
            assert(!locks);
            if(!sdk_error&&!s_radio.lifecycle.identity) {
                const double masks[]={0,1,31,-1,4294967295.0};
                for(unsigned i=0;i<sizeof(masks)/sizeof(*masks);i++) {
                    *a=JS_NewFloat64(ctx,masks[i]);*r=js_wifi_diagnostics_dump_driver_stats(ctx,NULL,1,a);
                    assert(*r==JS_TRUE && seen_mask==(masks[i]<0?UINT32_MAX:(uint32_t)masks[i]));
                }
            }
        }
        JS_PopGCRef(ctx,&ar);JS_PopGCRef(ctx,&rr);assert(!root_count&&!native_live);
        JS_FreeContext(ctx);free(heap);
    }
}
'''
COVERAGE_BOUNDARY = r'''
#define CONFIG_IDF_TARGET "esp32c3"
static unsigned capability_calls;
static const char *esp_get_idf_version(void) {return "test-idf";}
static JSValue js_wifi_capabilities(JSContext *ctx,JSValue *self,int argc,JSValue *argv) {
    assert(!self&&!argc&&!argv);capability_calls++;return JS_NewString(ctx,"current-build");
}
'''
COVERAGE_MAIN = r'''
static void check_counts(JSContext *ctx,JSValue value,const uint32_t *expected) {
    for(unsigned i=0;i<15;i++) {
        uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,value,s_wifi_coverage_fields[i]))&&n==expected[i]);
    }
}
int main(void) {
    int total=0;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(512*1024);JSContext *ctx=JS_NewContext(heap,512*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef rr,ar,ir;JSValue *r=JS_PushGCRef(ctx,&rr),*a=JS_PushGCRef(ctx,&ar),*item=JS_PushGCRef(ctx,&ir);
        capability_calls=0;inject=true;collect=true;calls=0;fail_at=nth;
        *r=js_wifi_diagnostics_idf_api_coverage(ctx,NULL,0,NULL);
        inject=false;collect=false;if(!nth){total=calls;assert(!JS_IsException(*r));}
        else assert(JS_IsException(*r));
        if(JS_IsException(*r)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else {
            assert(capability_calls==1);
            *item=JS_GetPropertyStr(ctx,*r,"total");check_counts(ctx,*item,s_wifi_coverage_total);
            const char *names[]={"headers","tasks","referenceVariants"};
            const wifi_coverage_row_t *rows[]={s_wifi_coverage_headers,s_wifi_coverage_tasks,s_wifi_coverage_variants};
            unsigned lengths[]={sizeof(s_wifi_coverage_headers)/sizeof(*s_wifi_coverage_headers),sizeof(s_wifi_coverage_tasks)/sizeof(*s_wifi_coverage_tasks),sizeof(s_wifi_coverage_variants)/sizeof(*s_wifi_coverage_variants)};
            for(unsigned group=0;group<3;group++) {
                *a=JS_GetPropertyStr(ctx,*r,names[group]);uint32_t length;
                assert(!JS_ToUint32(ctx,&length,JS_GetPropertyStr(ctx,*a,"length"))&&length==lengths[group]);
                for(unsigned i=0;i<length;i++) {
                    *item=JS_GetPropertyUint32(ctx,*a,i);JSCStringBuf b;
                    const char *name=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*item,"name"),&b);assert(name&&!strcmp(name,rows[group][i].name));
                    *item=JS_GetPropertyStr(ctx,*item,"counts");check_counts(ctx,*item,rows[group][i].counts);
                }
            }
        }
        assert(capability_calls<=1);
        *r=js_wifi_diagnostics_idf_api_coverage(ctx,NULL,1,NULL);assert(JS_IsException(*r));JS_GetException(ctx);
        JS_PopGCRef(ctx,&ir);JS_PopGCRef(ctx,&ar);JS_PopGCRef(ctx,&rr);assert(!root_count&&!native_live);
        JS_FreeContext(ctx);free(heap);
    }
}
'''
