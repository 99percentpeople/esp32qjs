
int main(int argc,char **argv) {
    assert(argc==5);bool observer=!strcmp(argv[1],"rssi"),expected=atoi(argv[3]),sdk_fail=atoi(argv[4]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,result_ref,value_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&result_ref),*value=JS_PushGCRef(ctx,&value_ref);
        *input=JS_Eval(ctx,argv[2],strlen(argv[2]),"controls",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        *value=JS_GetPropertyStr(ctx,*input,"length");int32_t count;assert(!JS_ToInt32(ctx,&count,*value) && count<=4);
        JSValue args[4];for(int i=0;i<count;++i)args[i]=JS_GetPropertyUint32(ctx,*input,i);
        reset();if(sdk_fail)native_fail_at=observer ? 1 : 2;
        /* SDK call count and VM allocation count are independent injection axes. */
        calls=0;fail_at=nth;collect=true;inject=true;
        *output=observer ? js_wifi_driver_set_rssi_threshold(ctx,NULL,count,args) : js_wifi_driver_set_inactive_time(ctx,NULL,count,args);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!expected);}
        if(!expected && !sdk_fail)assert(writes==0);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && sdk_fail) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*value=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t code;assert(!JS_ToInt32(ctx,&code,*value) && code==77);
                assert(s_radio.configuration.error==77 && s_radio.configuration.mutation_attempted);
            }
        } else {int32_t n;assert(!JS_ToInt32(ctx,&n,*output));int32_t requested;assert(!JS_ToInt32(ctx,&requested,args[observer ? 0 : 1]) && n==requested);}
        JS_PopGCRef(ctx,&value_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
