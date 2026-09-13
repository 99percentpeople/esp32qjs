
static bool text_is(JSContext *ctx,JSValue object,const char *key,const char *expected) {
    JSCStringBuf b;JSValue value=JS_GetPropertyStr(ctx,object,key);size_t length;
    const char *s=JS_ToCStringLen(ctx,&length,value,&b);
    return s && length==strlen(expected) && !memcmp(s,expected,length);
}
int main(int argc,char **argv) {
    assert(argc==4);unsigned expected=atoi(argv[2]);scenario=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,result_ref,details_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*output=JS_PushGCRef(ctx,&result_ref),*details=JS_PushGCRef(ctx,&details_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"restart",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        wait_calls=end_calls=execute_calls=status_calls=diagnostic_calls=0;waited_ms=0;
        wait_active=driver_started=s_wifi_configuration_cleanup=false;s_wifi_state.sta_detach_error=0;
        native_radio=(esp32_mquickjs_wifi_radio_status_t){.configuration={.stage="old-config",.error=88,.mutation_attempted=true}};
        assert(JS_IsException(js_wifi_driver_restart(ctx,NULL,2,input)) && !wait_calls && !execute_calls);JS_GetException(ctx);
        calls=0;fail_at=nth;inject=true;collect=true;
        *output=js_wifi_driver_restart(ctx,NULL,JS_IsUndefined(*input) ? 0 : 1,input);
        inject=false;collect=false;
        assert(!wait_active && execute_calls<=1 && end_calls==execute_calls && status_calls<=1);
        if(wait_calls)assert(waited_ms==expected);
        if(!expected)assert(!wait_calls && !execute_calls && !diagnostic_calls);
        if(!nth) {
            total=calls;
            assert(JS_IsException(*output)==(!expected || (scenario!=0 && scenario!=6)));
            if(expected && (!scenario || scenario==6))assert(driver_started && status_calls==1 && execute_calls==1);
            if(expected && scenario && scenario!=6) {
                *output=JS_GetException(ctx);
                assert(text_is(ctx,*output,"code","WIFI_RESTART_FAILED"));
                assert(text_is(ctx,*output,"operation","wifi.driver.restart"));
                *details=JS_GetPropertyStr(ctx,*output,"details");
                assert(JS_GetPropertyStr(ctx,*details,"lifecycleAdmitted")==((scenario>=3)?JS_TRUE:JS_FALSE));
                assert(JS_GetPropertyStr(ctx,*details,"checkpointAttempted")==((scenario>=4)?JS_TRUE:JS_FALSE));
                assert(JS_GetPropertyStr(ctx,*details,"cleanupPending")==((scenario>=3)?JS_TRUE:JS_FALSE));
                assert(JS_GetPropertyStr(ctx,*details,"restartRequired")==((scenario==5)?JS_TRUE:JS_FALSE));
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*details,"configuration"))==(scenario<4 || scenario==7));
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"password")));
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*details,"ssid")));
            }
        }
        if(JS_HasException(ctx))JS_GetException(ctx);
        /* VM result OOM after successful reconstruction does not undo or retry it. */
        if(driver_started)assert(execute_calls==1 && end_calls==1 && !s_wifi_configuration_cleanup);
        JS_PopGCRef(ctx,&details_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
