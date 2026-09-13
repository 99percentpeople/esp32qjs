
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv){
 assert(argc==6);int expected=atoi(argv[2]),success=atoi(argv[3]),secrets=atoi(argv[4]);int total=0;bool delivery_failure=false;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);
  JSGCRef out_ref;JSValue *out=JS_PushGCRef(ctx,&out_ref);
  memset(&s_radio,0,sizeof(s_radio));s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
  reads=writes=0;native_error=atoi(argv[5])?-77:0;calls=0;fail_at=nth;collect=inject=true;
  *out=JS_Eval(ctx,argv[1],strlen(argv[1]),"driver-config",JS_EVAL_RETVAL);inject=collect=false;
  if(!nth){total=calls;assert(reads+writes==expected);assert(JS_IsException(*out)==!success);}
  assert(reads+writes<=1);
  if(JS_IsException(*out)){assert(JS_HasException(ctx));JS_GetException(ctx);if(nth&&writes)delivery_failure=true;}
  else{
   assert(success);JSValue flag=JS_GetPropertyStr(ctx,*out,"secretsIncluded");assert(flag==(secrets?JS_TRUE:JS_FALSE));
   JSValue password=JS_GetPropertyStr(ctx,*out,"password");assert(secrets?JS_IsString(ctx,password):JS_IsNull(password));
   JSValue bytes=JS_GetPropertyStr(ctx,*out,"passwordBytes");assert(secrets?JS_IsArray(ctx,bytes):JS_IsNull(bytes));
   if(strstr(argv[1],"station")){
    JSValue secret=JS_GetPropertyStr(ctx,*out,"saeH2eIdentifierBytes");assert(secrets?JS_IsArray(ctx,secret):JS_IsNull(secret));
    if(writes){int32_t interval;assert(!JS_ToInt32(ctx,&interval,JS_GetPropertyStr(ctx,*out,"listenInterval"))&&interval==3);}
   }else{
    assert(JS_IsNull(JS_GetPropertyStr(ctx,*out,"ssid"))); /* Binary AP SSID stays lossless. */
    bytes=JS_GetPropertyStr(ctx,*out,"ssidBytes");int32_t length;assert(!JS_ToInt32(ctx,&length,JS_GetPropertyStr(ctx,bytes,"length"))&&length==3);
   }
  }
  JS_PopGCRef(ctx,&out_ref);JS_GC(ctx);assert(!root_count&&!native_live&&!depth&&!critical);JS_FreeContext(ctx);free(heap);
 }
 if(success&&strstr(argv[1],"setConfig"))assert(delivery_failure);
}
