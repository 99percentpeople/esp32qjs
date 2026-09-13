
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
