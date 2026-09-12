"""Deferred real-VM NAN query argument rooting, snapshots and Nth allocation.

Native cache availability is supplied at the Session boundary. The native SDK,
Session/Radio concurrency and shutdown gates have separate production fixtures.
"""
import re
import tempfile
import unittest
from test_wifi_nan_session import BASE, PREFIX, without_includes
from test_wifi_nan_query import query_types
from wireless_vm_fixture import CORE, build, extract, run


class NanQueryGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = BASE / 'src/modules/wifi_nan'
        code = PREFIX + '\n#include <math.h>\n#include "mquickjs_priv.h"\n' + query_types()
        code += without_includes(CORE / 'esp32_mquickjs_options.c')
        code += re.search(r'^#define SET\(.*$', (public / 'esp32_mquickjs_wifi_nan.c').read_text(), re.M).group(0) + '\n'
        code += 'static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){memset(p,0,n);}\n'
        for name in ('nan_service_string', 'nan_event_bytes'):
            code += extract((public / 'esp32_mquickjs_wifi_nan_service_public.inc').read_text(), name)
        code += extract((public / 'esp32_mquickjs_wifi_nan_service_options_public.inc').read_text(), 'nan_option_bytes')
        code += BOUNDARY + (public / 'esp32_mquickjs_wifi_nan_query_public.inc').read_text()
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_production_query_conversion_and_argument_roots_every_allocation(self):
        cases = [
            ('service', '["alpha",null]', 1), ('service', '[7,null]', 1),
            ('peers', '["alpha",null]', 1), ('peer', '[undefined,[2,3,4,5,6,7]]', 1),
            ('peer', '["alpha",[2,3,4,5,6,7]]', 1),
            ('peer', '["alpha",[2,3,4,5,6,7].map(function(x){gc();return x;})]', 1),
            ('peer', '[7,[2,3,4,5,6,7]]', 1),
            ('service', '[0,null]', 0), ('service', '[256,null]', 0), ('service', '[NaN,null]', 0),
            ('service', '[1.5,null]', 0), ('service', '[true,null]', 0), ('service', '[null,null]', 0),
            ('service', '["",null]', 0), ('service', '["alpha\\u0000",null]', 0),
            ('service', '[Array(257).join("x"),null]', 0),
            ('peer', '[7,[2,3,4]]', 0), ('peer', '[7,[0,0,0,0,0,0]]', 0),
            ('peer', '[7,[3,3,4,5,6,7]]', 0), ('peer', '[7,[2,3,4,5,6,256]]', 0),
            ('peer', '[7,[2,3,4,5,6,7,8]]', 0),
        ]
        for mode, expression, expected in cases:
            with self.subTest(mode=mode, expression=expression):
                run([str(self.binary), mode, expression, str(expected), '0'])

    def test_missing_and_native_failure_release_query_reference(self):
        for error in ('107', '102', '105'):
            run([str(self.binary), 'peers', '[7,null]', '1' if error == '107' else '0', error])


BOUNDARY = r'''
typedef struct{unsigned references;}esp32_mquickjs_wifi_nan_session_t;
static esp32_mquickjs_wifi_nan_session_t parent={.references=1};
static unsigned queries;
static int query_error;
static esp32_mquickjs_wifi_nan_session_t*nan_receiver(JSContext*ctx,JSValue value){(void)ctx;(void)value;return &parent;}
static bool esp32_mquickjs_wifi_nan_session_retain(esp32_mquickjs_wifi_nan_session_t*s){++s->references;return true;}
static void esp32_mquickjs_wifi_nan_session_release(esp32_mquickjs_wifi_nan_session_t*s){assert(s->references==2);--s->references;}
static JSValue nan_error(JSContext*ctx,const char*operation,esp32_mquickjs_wifi_nan_session_t*s,esp_err_t e,bool timeout){
 assert(s==&parent&&s->references==2&&e&&!timeout&&operation);return JS_ThrowInternalError(ctx,"native query failure");
}
static esp_err_t esp32_mquickjs_wifi_nan_session_query(esp32_mquickjs_wifi_nan_session_t*s,
 const esp32_mquickjs_wifi_nan_query_t*q,esp32_mquickjs_wifi_nan_query_result_t*out){
 assert(s==&parent&&s->references==2);++queries;
 assert(!q->service_name||!strcmp(q->service_name,"alpha"));
 if(q->kind==ESP32_MQUICKJS_NAN_QUERY_PEER)assert(!memcmp(q->peer,(uint8_t[6]){2,3,4,5,6,7},6));
 if(query_error)return query_error;
 *out=(esp32_mquickjs_wifi_nan_query_result_t){.service_id=7,.peer_count=15};strcpy(out->service_name,"alpha");
 for(unsigned i=0;i<15;i++)out->peers[i]=(struct nan_peer_record){.own_svc_id=7,.peer_svc_id=i+1,
  .peer_svc_type=ESP_NAN_PUBLISH,.peer_nmi={2,3,4,5,6,7},.ndp_id=i%2?0:9,.peer_ndi={2,9,8,7,6,5}};
 return ESP_OK;
}
'''

MAIN = r'''
int main(int argc,char**argv){
 assert(argc==5);int total=1;bool single=!strcmp(argv[1],"peer");query_error=atoi(argv[4]);
 for(int nth=0;nth<=total;nth++){
  void*heap=malloc(256*1024);JSContext*ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef input_ref,selector_ref,peer_ref,self_ref,result_ref,field_ref,row_ref;
  JSValue*input=JS_PushGCRef(ctx,&input_ref),*selector=JS_PushGCRef(ctx,&selector_ref),*peer=JS_PushGCRef(ctx,&peer_ref),
   *self=JS_PushGCRef(ctx,&self_ref),*result=JS_PushGCRef(ctx,&result_ref),*field=JS_PushGCRef(ctx,&field_ref),*row=JS_PushGCRef(ctx,&row_ref);
  *input=JS_Eval(ctx,argv[2],strlen(argv[2]),"query",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  *selector=JS_GetPropertyUint32(ctx,*input,0);*peer=JS_GetPropertyUint32(ctx,*input,1);*self=JS_NewObject(ctx);
  JSValue arguments[2]={single?*peer:*selector,*selector};
  calls=0;queries=0;fail_at=nth;inject=collect=true;
  *result=single?js_wifi_nan_get_peer_info(ctx,self,2,arguments):!strcmp(argv[1],"service")?
   js_wifi_nan_get_service_info(ctx,self,1,arguments):js_wifi_nan_get_peer_records(ctx,self,1,arguments);
  inject=collect=false;bool ok=!JS_IsException(*result);
  if(!nth){total=calls;assert(ok==(atoi(argv[3])!=0));}
  assert(parent.references==1);
  if(!ok){assert(JS_HasException(ctx));JS_GetException(ctx);}
  else if(query_error)assert(JS_IsNull(*result));
  else{
   *field=JS_GetPropertyStr(ctx,*result,"serviceId");int32_t id;assert(!JS_ToInt32(ctx,&id,*field)&&id==7);
   if(single){
    *field=JS_GetPropertyStr(ctx,*result,"peerMac");assert(JS_IsArray(ctx,*field));
    *field=JS_GetPropertyStr(ctx,*result,"ndpId");assert(!JS_ToInt32(ctx,&id,*field)&&id==9);
   }else{
    *field=JS_GetPropertyStr(ctx,*result,"peerCount");assert(!JS_ToInt32(ctx,&id,*field)&&id==15);
    if(!strcmp(argv[1],"peers")){
     *field=JS_GetPropertyStr(ctx,*result,"peers");assert(JS_IsArray(ctx,*field));
     *row=JS_GetPropertyUint32(ctx,*field,1);*field=JS_GetPropertyStr(ctx,*row,"peerDataMac");assert(JS_IsNull(*field));
     *field=JS_GetPropertyStr(ctx,*row,"ndpId");assert(JS_IsNull(*field));
    }
   }
   *field=JS_GetPropertyStr(ctx,*result,"credentials");assert(JS_IsUndefined(*field));
  }
  if(!nth&&!atoi(argv[3])&&!query_error)assert(!queries);
  JS_PopGCRef(ctx,&row_ref);JS_PopGCRef(ctx,&field_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&self_ref);
  JS_PopGCRef(ctx,&peer_ref);JS_PopGCRef(ctx,&selector_ref);JS_PopGCRef(ctx,&input_ref);
  JS_FreeContext(ctx);free(heap);assert(!root_count&&!native_live);
 }
}
'''
