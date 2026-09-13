
#undef JS_GetPropertyStr
int main(int argc,char **argv){
 assert(argc==5);bool expected=atoi(argv[2]);native_error=atoi(argv[3]);int count=atoi(argv[4]);
 int total=0;bool delivery_oom=false;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
  JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref);JSValue *output=JS_PushGCRef(ctx,&output_ref);
  *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"country",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  native_writes=0;calls=0;fail_at=nth;collect=inject=true;
  *output=js_wifi_driver_set_country_details(ctx,NULL,count,input);
  inject=collect=false;
  if(!nth){total=calls;assert(native_writes==(int)expected);assert(JS_IsException(*output)==(!expected||native_error));}
  assert(native_writes<=1);
  if(JS_IsException(*output)){
   assert(JS_HasException(ctx));JS_GetException(ctx);
   if(nth&&native_writes&&!native_error)delivery_oom=true;
  }else{
   assert(expected&&!native_error&&native_writes==1);
   JSValue power=JS_GetPropertyStr(ctx,*output,"maxTxPowerDbm");int32_t value;
   assert(!JS_ToInt32(ctx,&value,power)&&value==17);
  }
  JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_GC(ctx);
  assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 if(expected&&!native_error)assert(delivery_oom);
}
