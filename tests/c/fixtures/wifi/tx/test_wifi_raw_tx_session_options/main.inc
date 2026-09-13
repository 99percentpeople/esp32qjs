
int main(int argc,char **argv) {
    assert(argc==5);int total=1,expected=atoi(argv[3]);
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef value_ref,error_ref;JSValue *value=JS_PushGCRef(ctx,&value_ref),*error=JS_PushGCRef(ctx,&error_ref);
        *value=JS_Eval(ctx,argv[2],strlen(argv[2]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*value));
        session_options_t options={.capacity=999};uint32_t timeout=0;
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok=!strcmp(argv[1],"open") ? capture_open_options(ctx,*value,&options,&timeout) : capture_send_options(ctx,*value,&timeout);
        inject=false;collect=false;
        if(!nth){total=calls;assert(ok==expected);}
        if(ok) {
            assert(timeout>=1 && timeout<=60000);
            if(!strcmp(argv[1],"open"))assert(options.capacity>=1 && options.capacity<=128);
        } else {
            assert(JS_HasException(ctx));*error=JS_GetException(ctx);
            if(!strcmp(argv[1],"open"))assert(options.capacity==999);
            if(!nth && argv[4][0]) {
                *error=JS_GetPropertyStr(ctx,*error,"message");JSCStringBuf buffer;size_t length;
                const char *text=JS_ToCStringLen(ctx,&length,*error,&buffer);
                assert(text && length==strlen(argv[4]) && !memcmp(text,argv[4],length));
            }
        }
        JS_PopGCRef(ctx,&error_ref);JS_PopGCRef(ctx,&value_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
