
int main(int argc,char **argv){
 (void)argc;collect=atoi(argv[1]);int total=1;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef root_ref;JSValue *root=JS_PushGCRef(ctx,&root_ref);
  calls=0;fail_at=nth;moved_roots=0;inject=true;
  *root=js_wifi_driver_capabilities(ctx,NULL,0,NULL);inject=false;if(!nth)total=calls;
  if(nth){assert(JS_IsException(*root)&&JS_HasException(ctx));(void)JS_GetException(ctx);}
  else{
   assert(!JS_IsException(*root)&&!JS_HasException(ctx));if(collect)assert(moved_roots>0);
   assert(JS_GetPropertyStr(ctx,*root,"secretReadback")==JS_NewBool(SECRET_READ));
   JSValue operations=JS_GetPropertyStr(ctx,*root,"operations");int32_t n;
   assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,operations,"length"))&&n==sizeof(wifi_driver_operations)/sizeof(wifi_driver_operations[0]));
   bool scan_get_found=false,restore_found=false;
   for(int i=0;i<n;++i){JSValue op=JS_GetPropertyUint32(ctx,operations,i);JSCStringBuf pathbuf;
    const char *path=JS_ToCString(ctx,JS_GetPropertyStr(ctx,op,"jsPath"),&pathbuf);
    if(!strcmp(path,"wifi.driver.restore")){restore_found=true;assert(JS_GetPropertyStr(ctx,op,"available")==JS_TRUE);}
    if(!strcmp(path,"wifi.driver.getScanParameters")){scan_get_found=true;assert(JS_GetPropertyStr(ctx,op,"available")==JS_TRUE);}}
   assert(scan_get_found && restore_found);
  }
  JS_PopGCRef(ctx,&root_ref);JS_GC(ctx);assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 return 0;
}
