
int main(void){
 void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);
 JSGCRef ref;JSValue *v=JS_PushGCRef(ctx,&ref);*v=js_wifi_wps_capabilities(ctx,NULL,0,NULL);assert(!JS_IsException(*v));
 assert(JS_GetPropertyStr(ctx,*v,"sdkApRegistrar")==JS_NewBool(EXPECTED_REGISTRAR));
 assert(JS_GetPropertyStr(ctx,*v,"apRegistrar")==JS_NewBool(EXPECTED_REGISTRAR));
 assert(JS_GetPropertyStr(ctx,*v,"apRegistrar5GHz")==JS_NewBool(EXPECTED_5GHZ));
 assert(JS_GetPropertyStr(ctx,*v,"enrollee")==JS_TRUE);
 JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);return 0;
}
