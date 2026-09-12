"""Deferred real moving-GC and nth-allocation failure WAPI status conversion."""
import re
import tempfile
import unittest
from pathlib import Path
from wireless_vm_fixture import build, extract, run

BASE = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs'


class WiFiWapiGC(unittest.TestCase):
    def test_status_preserves_unknown_native_state_and_roots_every_result(self):
        header = (BASE / 'internal/esp32_mquickjs_wifi_wapi.h').read_text()
        status = re.search(r'typedef struct \{[^}]+\} esp32_mquickjs_wifi_wapi_status_t;', header).group(0)
        public = (BASE / 'src/modules/wifi/esp32_mquickjs_wifi_wapi_public.inc').read_text()
        code = 'typedef int esp_err_t;\n' + status + r'''
static bool s_wifi_configuration_cleanup;
static struct{bool runtime_cleanup_pending;}s_wifi_state;
static esp32_mquickjs_wifi_wapi_status_t snapshot;
static void esp32_mquickjs_wifi_wapi_sdk_status(esp32_mquickjs_wifi_wapi_status_t*out){*out=snapshot;}
'''
        code += re.search(r'^#define WAPI_SET[^\n]*', public, re.M).group(0) + '\n'
        code += extract(public, 'wifi_wapi_status')
        with tempfile.TemporaryDirectory() as directory:
            binary = build(directory, code, MAIN)
            run([str(binary)])


MAIN = r'''
int main(void){
 int total=1;
 for(int nth=0;nth<=total;nth++){
  void*heap=malloc(256*1024);JSContext*ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  snapshot=(esp32_mquickjs_wifi_wapi_status_t){.requested_enabled=true,.enabled=true,.uncertain=true,.cleanup_error=-74,.revision=UINT32_MAX};
  JSGCRef ref;JSValue*result=JS_PushGCRef(ctx,&ref);
  calls=0;fail_at=nth;inject=collect=true;*result=wifi_wapi_status(ctx);inject=collect=false;
  if(!nth){total=calls;assert(!JS_IsException(*result));}
  if(!JS_IsException(*result)){
   assert(JS_GetPropertyStr(ctx,*result,"enabled")==JS_NULL);
   assert(JS_GetPropertyStr(ctx,*result,"restartRequired")==JS_TRUE);
   assert(JS_GetPropertyStr(ctx,*result,"policyApplied")==JS_FALSE);
   assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"password")));
  }
  if(JS_HasException(ctx))JS_GetException(ctx);
  JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);assert(!root_count&&!native_live);
 }
}
'''
