"""Deferred actual VM deauth parser/error conversion, moving GC and allocation failures."""
import tempfile
import unittest
from wireless_vm_fixture import ROOT, CORE, build, extract, run


class WiFiAPDeauthGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
        wireless = (CORE / 'esp32_mquickjs_wireless_core.c').read_text()
        code = BOUNDARIES
        for name in ('wireless_hex_nibble', 'esp32_mquickjs_wireless_parse_address'):
            code += extract(wireless, name)
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += extract(source, 'js_wifi_deauth_client')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_inputs_and_error_allocation_do_not_repeat_native_mutation(self):
        for expression, args, valid in [
            ('"02:ab:cd:00:ef:01"', 1, True), ('"02:AB:CD:00:EF:01"', 1, True),
            ('null', 1, False), ('1', 1, False), ('"00:00:00:00:00:00"', 1, False),
            ('"ff:ff:ff:ff:ff:ff"', 1, False), ('"01:00:00:00:00:01"', 1, False),
            ('"02:ab:cd:00:ef:01\\u0000"', 1, False), ('"02:ab:cd:00:ef:01x"', 1, False),
            ('"02:ab:cd:00:ef:gg"', 1, False), ('"02:ab:cd:00:ef:01"', 0, False),
            ('"02:ab:cd:00:ef:01"', 2, False),
        ]:
            for mode in range(6):
                with self.subTest(expression=expression, args=args, mode=mode):
                    run([str(self.binary), expression, str(args), str(int(valid)), str(mode)])


BOUNDARIES = r'''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
static bool s_ap_cleanup_pending;
static int s_ap_lease,mode,sdk_calls;
static const char *s_ap_stage;
static int esp32_mquickjs_wifi_radio_ap_deauth(const void *lease,const uint8_t mac[6],
 bool *requested,bool *unknown,const char **stage){
 static const uint8_t expected[6]={2,0xab,0xcd,0,0xef,1};
 assert(lease==&s_ap_lease&&!memcmp(mac,expected,6));sdk_calls++;
 *requested=mode==0||mode==4;*unknown=mode==3;
 *stage=mode>=2?"deauth-request":NULL;return mode>=2?-77:0;}
'''

MAIN = r'''
int main(int argc,char **argv){assert(argc==5);int args=atoi(argv[2]),valid=atoi(argv[3]);mode=atoi(argv[4]);
 int total=0;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef input_ref,result_ref,error_ref,details_ref;
  JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref);
  JSValue *error=JS_PushGCRef(ctx,&error_ref),*details=JS_PushGCRef(ctx,&details_ref);
  *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"input",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  sdk_calls=0;s_ap_cleanup_pending=mode==5;s_ap_stage="old-cleanup";
  calls=0;fail_at=nth;inject=true;collect=true;
  *result=js_wifi_deauth_client(ctx,NULL,args,input);
  inject=false;collect=false;
  if(!nth){total=calls;assert(JS_IsException(*result)==(!valid||mode>=2));}
  assert(sdk_calls==(valid&&mode!=5));
  if(JS_IsException(*result)){
   *error=JS_GetException(ctx);
   if(!nth&&valid){
    *details=JS_GetPropertyStr(ctx,*error,"details");assert(JS_GetClassID(ctx,*details)==JS_CLASS_OBJECT);
    JSValue accepted=JS_GetPropertyStr(ctx,*details,"requestAccepted");
    assert(mode==3?JS_IsNull(accepted):accepted==(mode==4?JS_TRUE:JS_FALSE));
    assert(JS_GetPropertyStr(ctx,*details,"handoffUnknown")== (mode==3?JS_TRUE:JS_FALSE));
   }
  }else assert(*result==(mode==0?JS_TRUE:JS_FALSE));
  JS_PopGCRef(ctx,&details_ref);JS_PopGCRef(ctx,&error_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
  assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 return 0;
}
'''
