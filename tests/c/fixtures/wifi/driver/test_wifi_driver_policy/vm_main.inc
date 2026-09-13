
int main(int argc,char **argv) {
    assert(argc==3);unsigned query=atoi(argv[1]);bool arity=!strcmp(argv[2],"arity"),failed=!strcmp(argv[2],"sdk-error"),bad_iface=!strcmp(argv[2],"interface");
    bool boolean=!strcmp(argv[2],"true") || !strcmp(argv[2],"false");
    bool unsupported=query==2 && !CONFIG_ESP_COEX_POWER_MANAGEMENT;
    bool success=boolean && !unsupported;
    JSValue (*functions[])(JSContext *,JSValue *,int,JSValue *)={js_wifi_driver_set_dynamic_carrier_sense,js_wifi_driver_configure_11b_rate,js_wifi_driver_set_coexistence_power_management};
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef arg0_ref,arg1_ref,output_ref;JSValue *a=JS_PushGCRef(ctx,&arg0_ref),*b=JS_PushGCRef(ctx,&arg1_ref),*output=JS_PushGCRef(ctx,&output_ref);
        const char *expr=arity || failed || bad_iface ? "true" : argv[2];
        *b=JS_Eval(ctx,expr,strlen(expr),"policy",JS_EVAL_RETVAL);assert(!JS_IsException(*b));
        *a=query==1 ? JS_NewString(ctx,bad_iface ? "station\0suffix" : "station") : *b;
        /* Embedded NUL needs an explicit length; ordinary C strings would hide it. */
        if(query==1 && bad_iface)*a=JS_NewStringLen(ctx,"station\0suffix",14);
        policy_reset(query==1);if(failed)native_fail_at=1;
        calls=0;fail_at=nth;collect=true;inject=true;
        JSValue args[2]={*a,*b};
        *output=functions[query](ctx,NULL,arity || (bad_iface && query!=1) ? 0 : query==1 ? 2 : 1,args);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*output)==!success);}
        if(arity || bad_iface || (!boolean && !failed) || unsupported)assert(!native_calls);
        if(JS_IsException(*output)) {
            assert(JS_HasException(ctx));*output=JS_GetException(ctx);
            if(!nth && failed && !unsupported) {
                *output=JS_GetPropertyStr(ctx,*output,"details");*a=JS_GetPropertyStr(ctx,*output,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*a) && n==77 && s_radio.fault_error==77);
                *a=JS_GetPropertyStr(ctx,*output,"interface");if(query!=1)assert(JS_IsNull(*a));
            }
        } else assert(JS_IsBool(*output) && (*output==JS_TRUE)==!strcmp(argv[2],"true"));
        JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&arg1_ref);JS_PopGCRef(ctx,&arg0_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
    }
    return 0;
}
