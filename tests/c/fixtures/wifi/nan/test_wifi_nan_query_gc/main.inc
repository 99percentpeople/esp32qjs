
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
