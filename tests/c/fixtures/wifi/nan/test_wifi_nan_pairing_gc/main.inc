
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
