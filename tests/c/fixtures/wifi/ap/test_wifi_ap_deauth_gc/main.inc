
int main(int argc,char **argv){assert(argc==5);int args=atoi(argv[2]),valid=atoi(argv[3]);mode=atoi(argv[4]);
 int total=0;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef input_ref,result_ref,error_ref,details_ref;
  JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref);
  JSValue *error=JS_PushGCRef(ctx,&error_ref),*details=JS_PushGCRef(ctx,&details_ref);
  *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"input",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  sdk_calls=0;s_ap_cleanup_pending=mode==5;s_ap_stage="old-cleanup";
  calls=0;fail_at=nth;inject=true;collect=true;
  *result=js_wifi_deauth_client(ctx,NULL,args,input);
  inject=false;collect=false;
  if(!nth){total=calls;assert(JS_IsException(*result)==(!valid||mode>=2));}
  assert(sdk_calls==(valid&&mode!=5));
  if(JS_IsException(*result)){
   *error=JS_GetException(ctx);
   if(!nth&&valid){
    *details=JS_GetPropertyStr(ctx,*error,"details");assert(JS_GetClassID(ctx,*details)==JS_CLASS_OBJECT);
    JSValue accepted=JS_GetPropertyStr(ctx,*details,"requestAccepted");
    assert(mode==3?JS_IsNull(accepted):accepted==(mode==4?JS_TRUE:JS_FALSE));
    assert(JS_GetPropertyStr(ctx,*details,"handoffUnknown")== (mode==3?JS_TRUE:JS_FALSE));
   }
  }else assert(*result==(mode==0?JS_TRUE:JS_FALSE));
  JS_PopGCRef(ctx,&details_ref);JS_PopGCRef(ctx,&error_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
  assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 return 0;
}
