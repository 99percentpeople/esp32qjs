
static void mode_gc(void) {JS_GC(test_ctx);}
int main(int argc,char **argv) {
    assert(argc==4);unsigned total=0;bool expected=atoi(argv[2]);
    bool failed=!strcmp(argv[3],"sdk-error");
    int arity=!strcmp(argv[3],"arity-zero") ? 0 : !strcmp(argv[3],"arity-two") ? 2 : 1;
    for(unsigned nth=0;nth<=total;++nth) {
        uint8_t *heap=malloc(96*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"mode",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        mode_reset();mode_hook=mode_gc;if(failed)native_fail_at=2;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=js_wifi_driver_set_mode(ctx,NULL,arity,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity!=1 || (!expected && !failed))assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==77);
                assert(native_mode==WIFI_MODE_NULL && !s_radio.fault_stage);
            }
        } else {
            static const char *const names[]={"off","station","softAP","station+softAP"};
            JSCStringBuf buffer;const char *text=JS_ToCString(ctx,*output,&buffer);assert(text);
            assert(!strcmp(text,names[native_mode]) && !calls && s_radio.effective_mode==native_mode);
            assert(native_calls==(native_mode==WIFI_MODE_NULL ? 1U : 3U));
        }
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
