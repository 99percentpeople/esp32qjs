"""Deferred actual VM credential/vendor capture: strict inputs, moving GC, Nth OOM."""
import tempfile
import unittest

from test_wifi_nan_session import BASE, PREFIX, without_includes
from wireless_vm_fixture import CORE, build, extract, run


class NanServiceOptionsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        code = PREFIX + '\n#include <math.h>\n#include "mquickjs_priv.h"\n'
        code += 'static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}\n'
        code += without_includes(CORE / 'esp32_mquickjs_options.c')
        public = BASE / 'src/modules/wifi_nan'
        code += extract((public / 'esp32_mquickjs_wifi_nan_service_public.inc').read_text(), 'nan_service_string')
        code += 'typedef union {wifi_nan_publish_cfg_t publish;wifi_nan_subscribe_cfg_t subscribe;}esp32_mquickjs_wifi_nan_service_config_t;\n'
        code += 'static uint32_t esp32_mquickjs_wifi_radio_5ghz_channel_bit(uint8_t channel){(void)channel;return 0;}\n'
        code += (public / 'esp32_mquickjs_wifi_nan_service_options_public.inc').read_text()
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_security_capture_strict_credentials_and_getter_gc(self):
        cases = [
            ('({credentials:[{passphrase:"test-only-password"}]})', 1),
            ('({credentials:[{pmk:Array(33).join("1").split("").map(function(){return 7;})}]})', 1),
            ('({get credentials(){gc();return [{get passphrase(){gc();return "test-only-password";}}];}})', 1),
            ('({credentials:[]})', 0), ('({credentials:{length:1,0:{passphrase:"test-only-password"}}})', 0),
            ('({credentials:[{passphrase:"short"}]})', 0),
            ('({credentials:[{passphrase:"test-only-password",pmk:[1]}]})', 0),
            ('({credentials:[{cipher:"ncs-sk-256",passphrase:"test-only-password"}]})', 0),
            ('({credentials:[{pmk:[1,2,3]}]})', 0),
            ('({credentials:[{get pmk(){gc();throw new Error("sentinel");}}]})', 0),
            ('({credentials:[{passphrase:"test-only-password"}],groupDataProtection:1})', 0),
            ('({credentials:[{passphrase:"test-only-password"}],pairing:true})', 0),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'security', expression, str(expected)])

    def test_usd_parameters_and_getter_gc(self):
        cases = [('({})', 1), ('({ttlSeconds:0,channel:6,channels:[1,6,11],dwell:{nMin:5,nMax:10}})', 1),
                 ('({get channels(){gc();return [1,6,11];}})', 1), ('({channels:[1,1]})', 0),
                 ('({channels:[]})', 0), ('({channel:36})', 0), ('({ttlSeconds:2147483648})', 0),
                 ('({dwell:{nMin:11,nMax:10}})', 0), ('({dwell:{nMin:0}})', 0)]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'usd', expression, str(expected)])

    def test_vendor_capture_bounds_and_moving_getters(self):
        cases = [('({oui:[1,2,3],body:[]})', 1),
                 ('({get oui(){gc();return [1,2,3];},get body(){gc();return [4,5];}})', 1),
                 ('({oui:[1,2],body:[]})', 0), ('({oui:[1,2,3],body:[256]})', 0),
                 ('({oui:[1,2,3],body:[1.5]})', 0), ('({oui:[1,2,3],body:[],extra:true})', 0),
                 ('({oui:[1,2,3],body:Array(257).join("1").split("").map(function(){return 1;})})', 0)]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'vendor', expression, str(expected)])


MAIN = r'''
int main(int argc,char**argv){
 assert(argc==4);int total=1;bool security_mode=!strcmp(argv[1],"security"),usd_mode=!strcmp(argv[1],"usd");
 for(int nth=0;nth<=total;nth++){
  void*heap=malloc(256*1024);JSContext*ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef input_ref;JSValue*input=JS_PushGCRef(ctx,&input_ref);
  *input=JS_Eval(ctx,argv[2],strlen(argv[2]),"case",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  wifi_nan_discovery_security_params_t security={0};nan_vendor_ie_t vendor={0};uint8_t body[255]={0};
  calls=0;fail_at=nth;inject=collect=true;
  esp32_mquickjs_wifi_nan_service_config_t config={0};
  bool ok=usd_mode?nan_service_usd_options(ctx,&input_ref,true,true,&config):security_mode?nan_security_options(ctx,&input_ref,&security):nan_vendor_options(ctx,&input_ref,&vendor,body);
  inject=collect=false;if(!nth){total=calls;assert(ok==(atoi(argv[3])!=0));}
  if(JS_HasException(ctx))JS_GetException(ctx);
  if(security_mode&&!ok)for(size_t i=0;i<sizeof(security);i++)assert(!((uint8_t*)&security)[i]);
  if(ok&&security_mode)assert(security.num_credentials&&security.creds[0].csid==1);
  if(ok&&!security_mode&&!usd_mode)assert(vendor.body==body&&vendor.body_len<=255);
  esp32_mquickjs_wireless_secure_zero(&security,sizeof(security));
  JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);free(heap);assert(!root_count&&!native_live);
 }
}
'''
