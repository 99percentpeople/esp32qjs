
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
