"""Deferred MQuickJS capture/conversion with production rate helpers; AST only now."""
import tempfile
import unittest

from test_wifi_tx_rate import COMPONENT, rate_code
from test_wifi_rx_target import unit
from wireless_vm_fixture import CORE, build, extract, run


class WiFiTxRateOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code = rate_code('esp32c5/representative')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract(source, 'esp32_mquickjs_wifi_tx_rate_capture') + extract(source, 'tx_rate_status_to_js')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_types_getter_once_original_exception_and_gc_oom(self):
        cases = [
            ('({phy:"11g",rate:"6m"})', True),
            ('({phy:"he20",rate:"mcs0-short",ersu:true,dcm:false})', True),
            ('({phy:"11g",rate:"6m",dcm:false,ersu:false})', True),
            ('({phy:"11g",rate:"6m",dcm:0})', False),
            ('({phy:"11g",rate:"6m",ersu:null})', False),
            ('({phy:"11g",rate:"6m",dcm:true})', False),
            ('({phy:"ht20",rate:"mcs9-long"})', False),
            ('({phy:"ht20",rate:"6m"})', False),
            ('({phy:"11g",rate:"6m\\u0000"})', False),
            ('({phy:"11g\\u0000",rate:"6m"})', False),
            ('({phy:"11g",rate:8})', False),
            ('({phy:"11g"})', False), ('[]', False), ('null', False),
            ('({phy:"11g",rate:"6m",extra:1})', False),
            ('({get phy(){throw new Error("sentinel");},rate:"6m"})', False),
            ('({phy:"11g",get rate(){throw new Error("sentinel");}})', False),
            ('({phy:"11g",rate:"6m",get dcm(){throw new Error("sentinel");}})', False),
            ('(function(){var n=0,m=0;return {get phy(){if(++n!==1)throw new Error("twice");return "11g";},get rate(){if(++m!==1)throw new Error("twice");return "6m";}};})()', True),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), expression, str(int(expected)), 'sentinel' if 'sentinel' in expression else ''])

    def test_known_unknown_and_stale_record_converter_gc_oom(self):
        for case in ('known', 'unknown', 'stale', 'temporary'):
            run([str(self.binary), case, '1', ''])


MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==4);int total=1,expected=atoi(argv[2]);
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,result_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref);
        bool convert=!strcmp(argv[1],"known") || !strcmp(argv[1],"unknown") || !strcmp(argv[1],"stale") || !strcmp(argv[1],"temporary");
        if(!convert){*input=JS_Eval(ctx,argv[1],strlen(argv[1]),"rate",JS_EVAL_RETVAL);assert(!JS_IsException(*input));}
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;wifi_tx_rate_config_t config={0};
        if(convert) {
            esp32_mquickjs_wifi_tx_rate_record_t record={.known=strcmp(argv[1],"unknown")!=0,
                .generation=!strcmp(argv[1],"stale")?6:7,.write_identity=UINT32_MAX,
                .config={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M}};
            esp32_mquickjs_wifi_tx_rate_lease_t temporary={.generation=7,.identity=8,.write_identity=9,
                .previous={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_9M},.restore_pending=true,.restore_error=31};
            *result=tx_rate_status_to_js(ctx,WIFI_IF_STA,&record,7,!strcmp(argv[1],"temporary")?&temporary:NULL);ok=!JS_IsException(*result);
        } else ok=esp32_mquickjs_wifi_tx_rate_capture(ctx,*input,&config);
        inject=false;collect=false;
        if(!nth){total=calls;assert(ok==expected);}
        if(ok && convert) {
            bool known=!strcmp(argv[1],"known") || !strcmp(argv[1],"temporary");
            assert(JS_GetPropertyStr(ctx,*result,"known")==JS_NewBool(known));
            *input=JS_GetPropertyStr(ctx,*result,"temporaryLease");
            if(!strcmp(argv[1],"temporary"))assert(JS_GetPropertyStr(ctx,*input,"restorePending")==JS_TRUE);
            else assert(JS_IsNull(*input));
            *input=JS_GetPropertyStr(ctx,*result,"config");
            if(!known)assert(JS_IsNull(*input));
            else assert(JS_GetPropertyStr(ctx,*input,"dcm")==JS_FALSE);
            uint32_t n;*input=JS_GetPropertyStr(ctx,*result,"writeIdentity");
            assert(esp32_mquickjs_value_to_bounded_u32(ctx,*input,0,UINT32_MAX,&n) && n==UINT32_MAX);
        } else if(ok) {
            assert(esp32_mquickjs_wifi_tx_rate_valid(&config) && !config.dcm);
        } else {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && argv[3][0]) {
                *result=JS_GetPropertyStr(ctx,*result,"message");JSCStringBuf buffer;size_t length;
                const char *text=JS_ToCStringLen(ctx,&length,*result,&buffer);
                assert(text && length==strlen(argv[3]) && !memcmp(text,argv[3],length));
            }
        }
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
'''
