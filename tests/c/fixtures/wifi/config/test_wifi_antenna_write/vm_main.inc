
int main(int argc,char **argv){
 assert(argc==5||argc==6);bool gpio=atoi(argv[2]),valid=atoi(argv[3]);int total=0,arity=argc==6?atoi(argv[5]):1;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(96*1024);JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef input_ref,result_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref);
  *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"antenna",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  calls=native_writes=write_allocation_count=0;fail_at=nth;native_error=atoi(argv[4])?-77:0;collect=true;inject=true;
  *result=gpio?js_wifi_driver_set_antenna_gpio(ctx,NULL,arity,input):js_wifi_driver_set_antenna(ctx,NULL,arity,input);
  inject=false;collect=false;if(!nth)total=calls;
  if(!nth)assert(native_writes==(int)valid);
  if(!native_error&&valid&&native_writes){assert(!JS_IsException(*result)&&calls==write_allocation_count);}
  if(!valid)assert(JS_IsException(*result)&&!native_writes);
  if(JS_IsException(*result)){assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
  JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 return 0;
}
