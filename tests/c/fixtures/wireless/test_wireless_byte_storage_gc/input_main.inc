
int main(int argc,char **argv) {
    (void)argc;const char *memory_owner=atoi(argv[1])?"ble":NULL;
    void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    JSGCRef array_ref,view_ref;JSValue *array=JS_PushGCRef(ctx,&array_ref),*view=JS_PushGCRef(ctx,&view_ref);
    *array=JS_NewObject(ctx);*view=esp32_mquickjs_new_wireless_retained_byte_view(memory_owner,ctx,bytes,4,release,bytes);
    double invalid[]={-1,1.5,4294967297.0,NAN,INFINITY};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        JSValue value=JS_NewFloat64(ctx,invalid[i]);
        assert(JS_IsException(js_byte_view_get_uint8(ctx,view,1,&value)));(void)JS_GetException(ctx);
        JS_SetPropertyStr(ctx,*array,"length",JS_NewFloat64(ctx,invalid[i]));JS_SetPropertyUint32(ctx,*array,0,JS_NewInt32(ctx,7));
        esp32_mquickjs_byte_source_t source;uint8_t *owned=NULL;JSValue error;
        if(esp32_mquickjs_get_wireless_byte_source(memory_owner,ctx,*array,"test",&source,&owned,&error)) {
            fprintf(stderr,"invalid array length accepted: %g\n",invalid[i]);return 1;
        }
        assert(JS_HasException(ctx) && !owned);(void)JS_GetException(ctx);
        JS_SetPropertyStr(ctx,*array,"length",JS_NewInt32(ctx,1));
        JS_SetPropertyUint32(ctx,*array,0,JS_NewFloat64(ctx,invalid[i]));
        if(esp32_mquickjs_get_wireless_byte_source(memory_owner,ctx,*array,"test",&source,&owned,&error)) {
            fprintf(stderr,"invalid byte accepted: %g\n",invalid[i]);return 1;
        }
        assert(JS_HasException(ctx) && !owned);(void)JS_GetException(ctx);
    }
    JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&array_ref);JS_GC(ctx);assert(native_live==0 && released==1);
    JS_FreeContext(ctx);free(heap);
}
