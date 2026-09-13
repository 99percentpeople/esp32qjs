
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2]),get=!strncmp(argv[3],"get",3),failed=strstr(argv[3],"error")!=NULL,arity=strstr(argv[3],"arity")!=NULL;
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&output_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"mask",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        mask_reset();if(get)native_mask=UINT32_MAX;if(failed)native_fail_at=get ? 1 : 2;
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=get ? js_wifi_driver_get_event_mask(ctx,NULL,arity ? 1 : 0,input) : js_wifi_driver_set_event_mask(ctx,NULL,arity ? 0 : 1,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(arity || (!expected && !failed))assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*input=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*input) && n==77);
                *input=JS_GetPropertyStr(ctx,*output,"interface");assert(JS_IsNull(*input));
            }
        } else {
            double n;assert(!JS_ToNumber(ctx,&n,*output));assert(n==(get ? (double)UINT32_MAX : atof(argv[1])));
        }
        assert(!unsafe_writes);
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
