"""Deferred production setter capture with actual option helpers and moving GC."""
import tempfile
import unittest
from test_wifi_driver_phy import COMPONENT, phy_types
from test_wifi_rx_target import unit
from wireless_vm_fixture import CORE, build, extract, run


class WiFiDriverPhyOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        config = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code = phy_types('esp32c5/representative') + unit(CORE / 'esp32_mquickjs_options.c')
        code += extract(config, 'esp32_mquickjs_wifi_capture_protocol') + extract(driver, 'driver_phy_capture')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_shape_array_names_numbers_getters_and_gc_oom(self):
        cases = [
            (0, '["11b","11g","11n"]', True), (0, '[]', False),
            (0, '["11b","11b"]', False), (0, '["11b\\u0000"]', False),
            (0, '[1]', False), (0, '[undefined,"11b"]', False), (0, '"11b"', False),
            (1, '({ghz2:["11b"],ghz5:["11a","11n"]})', True),
            (1, '({})', False), (1, '({ghz2:undefined})', False),
            (1, '({ghz2:["11b"],extra:1})', False), (1, 'null', False),
            (1, '(function(){var n=0;return {get ghz2(){if(++n!==1)throw new Error("twice");return ["11b"];}};})()', True),
            (1, '({get ghz2(){throw new Error("sentinel");}})', False),
            (2, '20', True), (2, '40', True), (2, '21', False), (2, '20.5', False),
            (2, '"20"', False), (2, 'true', False), (2, '2147483668', False),
            (3, '({ghz2MHz:40,ghz5MHz:20})', True), (3, '({ghz2MHz:20,ghz5MHz:null})', False),
            (3, '({get ghz5MHz(){throw new Error("sentinel");}})', False),
        ]
        for kind, expression, expected in cases:
            with self.subTest(kind=kind, expression=expression):
                run([str(self.binary), str(kind), expression, str(int(expected))])


MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);int total=1;bool expected=atoi(argv[3]);
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef root_ref;JSValue *root=JS_PushGCRef(ctx,&root_ref);
        *root=JS_Eval(ctx,argv[2],strlen(argv[2]),"phy",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_phy_readback_t captured,zero={0};memset(&captured,0x55,sizeof(captured));
        calls=0;fail_at=nth;collect=true;inject=true;
        bool ok=driver_phy_capture(ctx,*root,(esp32_mquickjs_wifi_phy_query_t)atoi(argv[1]),&captured);
        inject=false;collect=false;
        if(!nth){total=calls;assert(ok==expected);}
        if(!ok) {
            assert(!memcmp(&captured,&zero,sizeof(captured)) && JS_HasException(ctx));
            *root=JS_GetException(ctx);
            if(!nth && strstr(argv[2],"sentinel")) {
                *root=JS_GetPropertyStr(ctx,*root,"message");JSCStringBuf buffer;
                const char *text=JS_ToCString(ctx,*root,&buffer);assert(text && !strcmp(text,"sentinel"));
            }
        } else assert(captured.bands && !(captured.bands & ~3U));
        JS_PopGCRef(ctx,&root_ref);JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
