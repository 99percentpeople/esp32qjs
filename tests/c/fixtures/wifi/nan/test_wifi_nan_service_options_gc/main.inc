
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
