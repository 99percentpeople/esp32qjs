
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[2]);
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,input_ref,error_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*input=JS_PushGCRef(ctx,&input_ref),*error=JS_PushGCRef(ctx,&error_ref);
        uint8_t *data=heap_caps_malloc(24,1);assert(data);memset(data,0,24);data[0]=0x80;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,data,24);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        bool convert=!strcmp(argv[1],"status");
        if(!convert){*input=JS_Eval(ctx,argv[1],strlen(argv[1]),"periodic",JS_EVAL_RETVAL);assert(!JS_IsException(*input));}
        esp32_mquickjs_wifi_raw_tx_periodic_options_t options={.interval_us=999};
        uint32_t timeout=0;uint8_t *bytes=NULL;size_t length=0;
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(convert) {
            status_t snapshot={.retired=true,.close_requested=true,.timer_quiesced=true,
                .ledger={.generation=55,.options={.interval_us=1000},.scheduled=5,.issued=2,.skipped_busy=2,.skipped_late=1}};
            *error=periodic_status_to_js(ctx,&snapshot);ok=!JS_IsException(*error);
        } else ok=periodic_options(ctx,&input_ref,&options,&timeout,&bytes,&length);
        inject=false;collect=false;
        if(!nth){total=calls;assert(ok==expected);}
        if(ok && convert) {
            assert(JS_GetPropertyStr(ctx,*error,"retired")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*error,"timerQuiesced")==JS_TRUE);
            JSValue generation=JS_GetPropertyStr(ctx,*error,"periodicGeneration");
            uint32_t n;assert(esp32_mquickjs_value_to_bounded_u32(ctx,generation,0,UINT32_MAX,&n) && n==55);
        } else if(ok) {
            assert(bytes && length==24 && bytes[0]==0x80 && options.interval_us>=1000);
            assert(timeout>=1 && timeout<=INT32_MAX);
            assert(options.stop_on_error==(strcmp(argv[4],"false")!=0));
        } else {
            assert(JS_HasException(ctx) && options.interval_us==999);*error=JS_GetException(ctx);
            if(!nth && argv[3][0]) {
                *error=JS_GetPropertyStr(ctx,*error,"message");JSCStringBuf buffer;size_t size;
                const char *text=JS_ToCStringLen(ctx,&size,*error,&buffer);
                assert(text && size==strlen(argv[3]) && !memcmp(text,argv[3],size));
            }
        }
        if(bytes)heap_caps_free(bytes);
        JS_PopGCRef(ctx,&error_ref);JS_PopGCRef(ctx,&input_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
