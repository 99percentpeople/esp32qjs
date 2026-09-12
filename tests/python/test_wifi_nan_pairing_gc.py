"""Deferred real MQuickJS pairing PIN and status conversion under moving GC/OOM."""
import re
import tempfile
import unittest
from test_wifi_nan_session import BASE, PREFIX, without_includes
from wireless_vm_fixture import build, extract, run


class NanPairingGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        code = PREFIX + '\n#define CONFIG_ESP_WIFI_NAN_PAIRING 1\n#include "mquickjs_priv.h"\n'
        for name in ('esp32_mquickjs_wifi_nan_pasn_sdk.h', 'esp32_mquickjs_wifi_nan_sdk.h',
                     'esp32_mquickjs_wifi_nan_ndp.h', 'esp32_mquickjs_wifi_nan_radio.h',
                     'esp32_mquickjs_wifi_nan_session.h', 'esp32_mquickjs_wifi_nan_discovery.h',
                     'esp32_mquickjs_wifi_nan_pairing.h'):
            code += without_includes(BASE / 'internal' / name)
        code += 'static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}\n'
        public = BASE / 'src/modules/wifi_nan'
        code += re.search(r'^#define SET\(.*$', (public / 'esp32_mquickjs_wifi_nan.c').read_text(), re.M).group(0) + '\n'
        code += extract((public / 'esp32_mquickjs_wifi_nan_service_public.inc').read_text(), 'nan_event_bytes')
        for name in ('nan_pairing_pin', 'nan_pairing_status_to_js', 'nan_credentials_to_js'):
            code += extract((public / 'esp32_mquickjs_wifi_nan_pairing_public.inc').read_text(), name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_pin_exact_digits_and_snapshot_each_allocation_failure(self):
        for value, expected in [('"001234"', 1), ('"000000"', 1), ('123456', 0),
                                ('"12345"', 0), ('"1234567"', 0), ('"12a456"', 0),
                                ('"12345\\u0000"', 0), ('undefined', 0)]:
            with self.subTest(value=value):
                run([str(self.binary), value, str(expected)])


MAIN = r'''
int main(int argc,char**argv){
 assert(argc==3);int total=1;
 for(int nth=0;nth<=total;nth++){
  void*heap=malloc(256*1024);JSContext*ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef input_ref,result_ref;JSValue*input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref);
  *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"pin",JS_EVAL_RETVAL);assert(!JS_IsException(*input));JS_GC(ctx);
  uint32_t pin=0;assert(nan_pairing_pin(ctx,*input,&pin)==(atoi(argv[2])!=0));
  esp32_mquickjs_wifi_nan_pairing_status_t status={.identity=UINT32_MAX,.service_identity=1,.session_identity=2,
    .peer={2,3,4,5,6,7},.bootstrap=true,.incoming=true,.peer_accepted=true,
    .native={.identity=99,.authenticated=true,.paired=true,.traffic_pending=true}};
  calls=0;fail_at=nth;inject=collect=true;*result=nan_pairing_status_to_js(ctx,&status);inject=collect=false;
  if(!nth){total=calls;assert(!JS_IsException(*result));}
  if(!JS_IsException(*result)){
   JSValue field=JS_GetPropertyStr(ctx,*result,"pin");assert(JS_IsUndefined(field));
   field=JS_GetPropertyStr(ctx,*result,"trafficPending");assert(field==JS_TRUE);
   field=JS_GetPropertyStr(ctx,*result,"ready");assert(field==JS_FALSE);
   field=JS_GetPropertyStr(ctx,*result,"bootstrap");assert(field==JS_TRUE);
   field=JS_GetPropertyStr(ctx,*result,"peerAccepted");assert(field==JS_TRUE);
   field=JS_GetPropertyStr(ctx,*result,"confirmed");assert(field==JS_FALSE);
  }
  if(JS_HasException(ctx))JS_GetException(ctx);
  esp32_mquickjs_wifi_nan_credentials_t credentials={.count=2,.entries={{.identity=301,.peer={2,3},.expires_us=10000000},{.identity=302,.peer={2,4}}}};
  calls=0;fail_at=nth;inject=collect=true;*result=nan_credentials_to_js(ctx,&credentials);inject=collect=false;
  if(!nth&&calls>total)total=calls;
  if(!JS_IsException(*result)){
   JSGCRef row_ref;JSValue*row=JS_PushGCRef(ctx,&row_ref);*row=JS_GetPropertyUint32(ctx,*result,0);
   JSValue field=JS_GetPropertyStr(ctx,*row,"npk");assert(JS_IsUndefined(field));
   field=JS_GetPropertyStr(ctx,*row,"peerNik");assert(JS_IsUndefined(field));JS_PopGCRef(ctx,&row_ref);
  }
  if(JS_HasException(ctx))JS_GetException(ctx);
  esp32_mquickjs_wireless_secure_zero(&pin,sizeof(pin));
  JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);free(heap);
  assert(!root_count&&!native_live);
 }
}
'''
