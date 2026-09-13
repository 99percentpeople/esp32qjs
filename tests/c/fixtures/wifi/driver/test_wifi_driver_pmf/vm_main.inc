
static void pmf_gc(void) {JS_GC(test_ctx);}
int main(int argc,char **argv) {
    assert(argc==4);unsigned total=0;bool expected=atoi(argv[2]);
    bool failed=!strcmp(argv[3],"sdk-error"),security=!strcmp(argv[3],"security");
    int arity=!strcmp(argv[3],"arity-zero") ? 0 : !strcmp(argv[3],"arity-two") ? 2 : 1;
    for(unsigned nth=0;nth<=total;++nth) {
        void *heap=malloc(96*1024);JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"pmf",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        pmf_reset();pmf_hook=pmf_gc;if(failed)native_fail_at=2;if(security)configs[0].sta.threshold.authmode=WIFI_AUTH_WPA3_PSK;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=js_wifi_driver_disable_pmf(ctx,NULL,arity,input);inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity!=1 || (!expected && !failed && !security))assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && (failed || security)) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==(failed ? 77 : ESP_ERR_NOT_SUPPORTED));
                *input=JS_GetPropertyStr(ctx,*output,"interface");JSCStringBuf b;const char *name=JS_ToCString(ctx,*input,&b);assert(name && !strcmp(name,"station"));
            }
        } else assert(JS_IsUndefined(*output) && native_calls==3 && writes==1 && !calls);
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
