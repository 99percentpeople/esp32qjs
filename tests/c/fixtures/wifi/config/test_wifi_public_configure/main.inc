
#undef esp32_mquickjs_memory_payload_free
static bool text_is(JSContext *ctx,JSValue object,const char *key,const char *expected) {
    JSCStringBuf b;JSValue value=JS_GetPropertyStr(ctx,object,key);size_t length;
    const char *s=JS_ToCStringLen(ctx,&length,value,&b);
    return s && length==strlen(expected) && !memcmp(s,expected,length);
}
int main(void) {
    for(scenario=0;scenario<=8;scenario++) {
        int total=1;
        for(int nth=0;nth<=total;nth++) {
            void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
            test_ctx=ctx;JSGCRef input_ref,result_ref,details_ref;
            JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*details=JS_PushGCRef(ctx,&details_ref);
            *input=JS_NewObject(ctx);apply_calls=status_calls=diagnostic_calls=free_calls=0;driver_started=false;
            s_wifi_configuration_cleanup=false;
            native_radio=(esp32_mquickjs_wifi_radio_status_t){
                .configuration={.stage="old-config",.persistent_mutation_possible=true},
                .activation={.stage="old-activation",.mutation_attempted=true}};
            /* Wrong arity has no native allocation or dispatch. */
            assert(JS_IsException(js_wifi_configure(ctx,NULL,0,NULL)) && !native_live && !apply_calls);JS_GetException(ctx);
            calls=0;fail_at=nth;inject=true;collect=true;
            *result=js_wifi_configure(ctx,NULL,1,input);
            inject=false;collect=false;
            assert(!native_live && free_calls<=1 && apply_calls<=1 && status_calls<=1);
            if(!nth) {
                total=calls;
                if(scenario==0)assert(!JS_IsException(*result) && driver_started && apply_calls==1 && status_calls==1);
                else {
                    assert(JS_IsException(*result) && JS_HasException(ctx));*result=JS_GetException(ctx);
                    if(scenario!=6) {
                        assert(text_is(ctx,*result,"operation","wifi.configure"));
                        assert(text_is(ctx,*result,"code",scenario>=7?"WIFI_CONFIG_UNSUPPORTED":"WIFI_CONFIG_FAILED"));
                        *details=JS_GetPropertyStr(ctx,*result,"details");assert(JS_GetClassID(ctx,*details)>=0);
                        assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"password")) && JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"ssid")));
                        assert((!JS_IsNull(JS_GetPropertyStr(ctx,*details,"configuration")))==(scenario>=3 && scenario<=5));
                        assert((!JS_IsNull(JS_GetPropertyStr(ctx,*details,"activation")))==(scenario==4));
                        if(scenario>=7)assert(text_is(ctx,*details,"stage","capture"));
                        if(scenario==8)assert(text_is(ctx,*details,"option","heDcmSet"));
                    }
                    assert(!driver_started && !status_calls);
                }
                assert(free_calls==1 && diagnostic_calls==(scenario>=1 && scenario<=5));
                assert(apply_calls==(scenario<=5));
            } else {
                assert(JS_IsException(*result) && JS_HasException(ctx));JS_GetException(ctx);
                /* Result/error allocation cannot replay apply or implicit cleanup. */
                if(scenario==0 && apply_calls)assert(driver_started && status_calls==1);
                if(scenario>=2 && scenario<=5 && apply_calls)assert(s_wifi_configuration_cleanup);
            }
            JS_PopGCRef(ctx,&details_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
            JS_GC(ctx);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
        }
    }
    return 0;
}
