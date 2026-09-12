"""Real production metadata converter with VM allocation/GC faults; run deferred."""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, build, extract, run
from test_wifi_config_controls import structure

MAIN = r'''
int main(void) {
    int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;JSGCRef ref;JSValue *value=JS_PushGCRef(ctx,&ref);
        esp32_mquickjs_wifi_radio_config_result_t native={0};
        assert(JS_IsNull(wifi_make_configuration_status(ctx,&native)));
        native=(esp32_mquickjs_wifi_radio_config_result_t){.stage="tx-power-readback",.error=-77,
            .mutation_attempted=true,.rollback_attempted=true,.rollback_stage="rollback-tx-power",.rollback_error=-88};
        calls=0;fail_at=nth;collect=true;inject=true;
        *value=wifi_make_configuration_status(ctx,&native);
        inject=false;collect=false;
        if(!nth) {
            total=calls;assert(!JS_IsException(*value) && !JS_HasException(ctx));
            JSCStringBuf b;const char *stage=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*value,"stage"),&b);
            assert(stage && !strcmp(stage,"tx-power-readback"));
            int32_t error;assert(!JS_ToInt32(ctx,&error,JS_GetPropertyStr(ctx,*value,"error")) && error==-77);
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*value,"password")));
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*value,"ssid")));
            assert(moved_roots>0);
        } else { assert(JS_IsException(*value) && JS_HasException(ctx));JS_GetException(ctx); }
        JS_PopGCRef(ctx,&ref);JS_GC(ctx);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
'''


class WiFiActivationStatus(unittest.TestCase):
    def test_status_metadata_allocation_failure_and_moving_gc(self):
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        body = 'typedef int esp_err_t;\n' + structure(header, 'esp32_mquickjs_wifi_radio_config_result_t')
        body += extract(wifi, 'wifi_make_configuration_status')
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, body, MAIN)
            run([str(binary)])
