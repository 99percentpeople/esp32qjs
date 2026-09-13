
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2]);int total=1;
    const char *memory_owner=atoi(argv[3])?"wifi.csi":NULL;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        inject=false;released=destroyed=opened=closed=0;calls=0;fail_at=nth;moved_roots=0;open_mode=0;
        JSGCRef result_ref,owner_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*owner=JS_PushGCRef(ctx,&owner_ref);
        *result=*owner=JS_UNDEFINED;
        if(mode>=2) *owner=esp32_mquickjs_new_wireless_retained_byte_view(memory_owner,ctx,bytes,4,release,bytes);
        if(mode>=3) *result=esp32_mquickjs_new_wireless_byte_span_source(memory_owner,ctx,*owner,&ops,bytes);
        uint8_t *owned=NULL;if(mode==0) { owned=heap_caps_malloc(4,1);memcpy(owned,bytes,4); }
        inject=true;JSValue value=JS_UNDEFINED;
        if(mode==0) value=esp32_mquickjs_new_wireless_owned_byte_view(memory_owner,ctx,owned,4);
        if(mode==1) value=esp32_mquickjs_new_wireless_retained_byte_view(memory_owner,ctx,bytes,4,release,bytes);
        if(mode==2) value=esp32_mquickjs_new_wireless_byte_span_source(memory_owner,ctx,*owner,&ops,bytes);
        esp32_mquickjs_byte_span_source_t span={0};
        if(mode>=3 && mode<=5) {
            open_mode=mode-3;
            bool ok=esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&span,&value);
            assert(ok==!JS_IsException(value));
        }
        if(mode==6) value=js_byte_span_source_close(ctx,result,0,NULL);
        inject=false;
        if(mode<=2)*result=value;
        if(nth==0)total=calls;
        if(nth || mode==4 || mode==5) {
            assert(JS_IsException(value) && JS_HasException(ctx));(void)JS_GetException(ctx);
        } else assert(!JS_IsException(value) && !JS_HasException(ctx));
        if(mode<=1 && !nth) {
            const uint8_t *data;size_t size;
            assert(esp32_mquickjs_byte_view_acquire_read(ctx,*result,"test",&data,&size));assert(size==4 && !memcmp(data,bytes,4));
            assert(js_byte_view_close(ctx,result,0,NULL)==JS_TRUE);
            assert(js_byte_view_close(ctx,result,0,NULL)==JS_TRUE);
            assert(released==0 && !esp32_mquickjs_byte_view_is_open(ctx,*result));
            esp32_mquickjs_byte_view_release_read(ctx,*result);
        }
        if(mode==3 && !nth) {
            *owner=JS_UNDEFINED;JS_GC(ctx);assert(released==0);
            assert(JS_IsException(js_byte_span_source_close(ctx,result,0,NULL)));(void)JS_GetException(ctx);
            esp32_mquickjs_byte_span_t chunk;
            assert(esp32_mquickjs_byte_span_source_next(ctx,&span,&chunk));assert(chunk.length==4);
            esp32_mquickjs_byte_span_source_close(ctx,&span);esp32_mquickjs_byte_span_source_close(ctx,&span);assert(closed==1);
        }
        if(mode>=3) { assert(js_byte_span_source_close(ctx,result,0,NULL)==JS_TRUE);assert(destroyed==1); }
        JS_PopGCRef(ctx,&owner_ref);JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);
        assert(root_count==0 && native_live==0);
        if(mode)assert(released==1);
        if(mode>=2)assert(destroyed==1);
        JS_FreeContext(ctx);free(heap);
    }
    printf("byte storage mode=%d gc=%d boundaries=%d\n",mode,collect,total);
}
