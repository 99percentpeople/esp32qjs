"""Deferred real-VM DPP converters: Nth allocation failure and moving GC.

Links production Session storage/copy/commit and public converters. Injects
received rows at the native result boundary; no provisioning/crypto/RF claim.
"""
import re
import tempfile
import unittest
from test_wifi_dpp_session import ROOT, TYPES, headers, unit
from test_wifi_dpp_connection import ROW
from test_wifi_ssid_result import BOUNDARIES as LINK_BOUNDARIES
from wireless_vm_fixture import CORE, build, extract, run


class DppCaptureGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        public = (ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp.c').read_text()
        native = (ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c').read_text()
        code = TYPES.replace('typedef struct {uint8_t ssid[32],ssid_len,key[128];} esp_dpp_config_data_t;', ROW)
        code += headers() + BOUNDARIES
        link_boundaries = LINK_BOUNDARIES.replace('#define ESP_ERR_INVALID_RESPONSE -7\n', '')
        link_boundaries = re.sub(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_link_snapshot_t;', '', link_boundaries)
        code += link_boundaries
        a = native.index('struct esp32_mquickjs_wifi_dpp_session {')
        b = native.index('static void dpp_session_close_locked', a)
        code += native[a:b]
        for name in ('dpp_session_close_locked', 'esp32_mquickjs_wifi_dpp_open_runtime',
                     'esp32_mquickjs_wifi_dpp_session_create', 'esp32_mquickjs_wifi_dpp_session_release',
                     'esp32_mquickjs_wifi_dpp_session_close', 'esp32_mquickjs_wifi_dpp_session_status',
                     'esp32_mquickjs_wifi_dpp_session_uri', 'esp32_mquickjs_wifi_dpp_session_config',
                     'esp32_mquickjs_wifi_dpp_session_configs_commit',
                     'esp32_mquickjs_wifi_dpp_session_connection_result'):
            code += extract(native, name)
        wifi_base = ROOT / 'src/modules/wifi'
        config = (wifi_base / 'esp32_mquickjs_wifi_config.c').read_text()
        wifi = (wifi_base / 'esp32_mquickjs_wifi.c').read_text()
        code += ''.join(extract(config, name) for name in (
            'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties'))
        code += ''.join(extract(wifi, name) for name in (
            'wifi_negotiated_phy_name', 'wifi_link_snapshot_valid', 'wifi_set_link_properties',
            'esp32_mquickjs_wifi_make_connect_result'))
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += re.search(r'^#define SET\(.*$', public, re.M).group(0) + '\n'
        code += 'typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define DPP_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        for name in ('dpp_identity', 'dpp_status_to_js', 'dpp_watch_to_js', 'dpp_error',
                     'dpp_bytes', 'dpp_uri_to_js', 'dpp_configurations_to_js'):
            code += extract(public, name)
        code += re.search(r'typedef enum \{[^}]*\} dpp_operation_t;', public).group(0)
        a = public.index('struct esp32_mquickjs_future_driver_state {')
        code += public[a:public.index('\n};', a) + 3]
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += extract(public, 'dpp_operation_name') + extract(public, 'dpp_finish')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_transactional_results_gc_and_every_conversion_allocation(self):
        for mode in ('uri', 'configurations', 'status', 'observation', 'error', 'closed',
                     'consumed', 'bad-count', 'bad-length', 'bad-connector',
                     'connection', 'disconnected-result'):
            with self.subTest(mode=mode):
                run([str(self.binary), mode])


BOUNDARIES = r'''
static bool session_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!session_locked);session_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(session_locked);session_locked=false;}while(0)
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static const char *esp_err_to_name(int e){(void)e;return "injected";}
esp_err_t esp32_mquickjs_wifi_dpp_worker_validate(const esp32_mquickjs_wifi_dpp_worker_options_t *o){
 assert(o&&!strcmp(o->channels,"6"));return ESP_OK;}
static esp32_mquickjs_wifi_dpp_connection_status_t current_connection;
esp_err_t esp32_mquickjs_wifi_dpp_connect_status(const esp32_mquickjs_wifi_radio_operation_t *owner,
 uint32_t generation,esp32_mquickjs_wifi_dpp_connection_status_t *out){
 assert(!session_locked&&owner->identity==13&&generation==23);*out=current_connection;return ESP_OK;}
'''
MAIN = r'''
int main(int argc,char **argv){
 assert(argc==2);int total=1;bool uri_mode=!strcmp(argv[1],"uri"),config_mode=!strcmp(argv[1],"configurations");
 bool connection_mode=!strcmp(argv[1],"connection"),disconnected_mode=!strcmp(argv[1],"disconnected-result");
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(512*1024);JSContext *ctx=JS_NewContext(heap,512*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef result_ref,bytes_ref,entry_ref;
  JSValue *result=JS_PushGCRef(ctx,&result_ref),*bytes=JS_PushGCRef(ctx,&bytes_ref),*entry=JS_PushGCRef(ctx,&entry_ref);
  assert(!esp32_mquickjs_wifi_dpp_open_runtime());
  esp32_mquickjs_wifi_dpp_worker_options_t options={.channels="6"};esp32_mquickjs_wifi_dpp_session_t *s=NULL;
  assert(!esp32_mquickjs_wifi_dpp_session_create(&options,false,1000,&s));
  s->status.uri_ready=s->status.configs_ready=true;s->status.config_count=3;s->status.uri_length=6;memcpy(s->uri,"DPP:X;",7);
  s->status.native.worker.native.identity=UINT64_MAX;
  for(unsigned i=0;i<3;i++){
   esp_dpp_config_data_t *r=&s->configs[i];r->ssid_len=32;memset(r->ssid,255,32);r->ssid[1]=0;
   r->password_len=64;memset(r->password,'P'+i,64);r->password[63]=0;r->akm=6;
   memcpy(r->connector,"secret-jws",11);r->connector_len=10;
   r->net_access_key_len=r->c_sign_key_len=128;memset(r->net_access_key,0xa5,128);memset(r->c_sign_key,0x5a,128);
   r->net_access_key_expiry=UINT64_MAX;r->curr_chan=11;
  }
  if(!strcmp(argv[1],"bad-count"))s->status.config_count=4;
  if(!strcmp(argv[1],"bad-length"))s->configs[2].password_len=65;
  if(!strcmp(argv[1],"bad-connector"))s->configs[2].connector[2]=0;
  if(!strcmp(argv[1],"closed"))esp32_mquickjs_wifi_dpp_session_close(s,false);
  if(!strcmp(argv[1],"consumed"))assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
  esp32_mquickjs_future_driver_state_t future={.session=s,.operation=DPP_CONNECT};
  if(connection_mode||disconnected_mode){
   s->status.connected=s->status.connection_verified=s->status.connection_requested=true;
   s->status.configuration_index=2;s->status.connection_generation=s->connection_generation=23;
   s->status.authentication=ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;s->operation.identity=13;
   s->connection_started_us=100;s->connection_completed_us=200100;
   s->connection_link=(esp32_mquickjs_wifi_link_snapshot_t){.valid=true,.ssid={'A'},.ssid_len=1,.bssid={2,3,4,5,6,7},.channel=6,.aid=9};
   current_connection=(esp32_mquickjs_wifi_dpp_connection_status_t){.generation=23,.connected=!disconnected_mode,.link=s->connection_link};
  }
  calls=0;fail_at=nth;inject=collect=true;bool ok;
  if(!strcmp(argv[1],"status"))*result=dpp_status_to_js(ctx,&s->status);
  else if(!strcmp(argv[1],"observation")){dpp_watch_event_t e={.sequence=9,.status=s->status};*result=dpp_watch_to_js(ctx,&e,NULL);}
  else if(!strcmp(argv[1],"error"))*result=dpp_error(ctx,"WiFiDppSession.receive",s,ESP_ERR_TIMEOUT,true);
  else if(connection_mode||disconnected_mode)*result=dpp_finish(ctx,&future);
  else *result=uri_mode?dpp_uri_to_js(ctx,s):dpp_configurations_to_js(ctx,s);
  ok=!JS_IsException(*result);if(!nth){total=calls;assert(ok==(uri_mode||config_mode||connection_mode||!strcmp(argv[1],"status")||!strcmp(argv[1],"observation")));}
  inject=collect=false;if(!ok){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
  if(uri_mode){
   if(!ok){assert(!s->status.uri_consumed&&!memcmp(s->uri,"DPP:X;",7));*result=dpp_uri_to_js(ctx,s);assert(!JS_IsException(*result));}
   assert(s->status.uri_consumed&&!s->status.uri_ready);for(size_t i=0;i<sizeof(s->uri);i++)assert(!s->uri[i]);
   *bytes=JS_GetPropertyStr(ctx,*result,"uri");JSCStringBuf buf;size_t n;const char *v=JS_ToCStringLen(ctx,&n,*bytes,&buf);assert(v&&n==6&&!memcmp(v,"DPP:X;",6));
  }
  if(config_mode){
   if(!ok){assert(!s->status.configs_consumed&&s->configs[2].net_access_key[127]==0xa5);*result=dpp_configurations_to_js(ctx,s);assert(!JS_IsException(*result));}
   assert(s->status.configs_consumed&&!s->status.configs_ready&&s->configs[2].net_access_key[127]==0xa5);
   *bytes=JS_GetPropertyStr(ctx,*result,"configurations");assert(JS_IsUndefined(JS_GetPropertyUint32(ctx,*bytes,3)));*entry=JS_GetPropertyUint32(ctx,*bytes,2);
   *bytes=JS_GetPropertyStr(ctx,*entry,"ssidBytes");double v;assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,1))&&v==0);
   assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,31))&&v==255);
   *bytes=JS_GetPropertyStr(ctx,*entry,"netAccessKeyBytes");assert(!JS_ToNumber(ctx,&v,JS_GetPropertyUint32(ctx,*bytes,127))&&v==165);
   *bytes=JS_GetPropertyStr(ctx,*entry,"netAccessKeyExpiry");assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*bytes,"high"))&&v==4294967295.0);
   assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*bytes,"low"))&&v==4294967295.0);
   assert(JS_IsException(dpp_configurations_to_js(ctx,s)));(void)JS_GetException(ctx);
  }
  if(!strncmp(argv[1],"bad-",4))assert(!s->status.configs_consumed);
  if(connection_mode){
   /* Failed JS delivery leaves the exact native connection and credentials
    * alive; retry converts the same result through the real Future finish. */
   assert(s->status.connected&&!s->status.closing&&s->connection_generation==23&&s->configs[2].net_access_key[127]==0xa5);
   if(!ok){*result=dpp_finish(ctx,&future);assert(!JS_IsException(*result));}
   assert(JS_GetPropertyStr(ctx,*result,"connected")==JS_TRUE);double v;
   assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*result,"configurationIndex"))&&v==2);
   assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*result,"connectionGeneration"))&&v==23);
   assert(!JS_ToNumber(ctx,&v,JS_GetPropertyStr(ctx,*result,"elapsedMs"))&&v==200);
   *bytes=JS_GetPropertyStr(ctx,*result,"authentication");JSCStringBuf buf;size_t n;
   const char *value=JS_ToCStringLen(ctx,&n,*bytes,&buf);assert(value&&n==3&&!memcmp(value,"dpp",3));
   assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"password"))&&JS_IsUndefined(JS_GetPropertyStr(ctx,*result,"connector")));
  }
  if(ok&&(!strcmp(argv[1],"status")||!strcmp(argv[1],"observation"))){
   *bytes=!strcmp(argv[1],"status")?*result:JS_GetPropertyStr(ctx,*result,"status");
   assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"uri"))&&JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"configurations")));
   assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"connector"))&&JS_IsUndefined(JS_GetPropertyStr(ctx,*bytes,"netAccessKeyBytes")));
  }
  esp32_mquickjs_wifi_dpp_session_close(s,false);for(size_t i=0;i<sizeof(s->configs);i++)assert(!((uint8_t*)s->configs)[i]);
  esp32_mquickjs_wifi_dpp_session_release(s);assert(!s_dpp_handles&&!native_live&&!session_locked);
  JS_PopGCRef(ctx,&entry_ref);JS_PopGCRef(ctx,&bytes_ref);JS_PopGCRef(ctx,&result_ref);
  JS_FreeContext(ctx);free(heap);assert(!root_count);
 }
 return 0;
}
'''
