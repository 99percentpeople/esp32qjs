
int main(int argc,char **argv) {
    assert(argc==3);unsigned query=atoi(argv[1]);bool failed=!strcmp(argv[2],"sdk-error"),arity=!strcmp(argv[2],"arity"),decode=!strcmp(argv[2],"decode");
    JSValue (*functions[])(JSContext *,JSValue *,int,JSValue *)={js_wifi_driver_get_mode,js_wifi_driver_get_country,js_wifi_driver_get_channel,js_wifi_driver_get_home_channel};
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,value_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*value=JS_PushGCRef(ctx,&value_ref);
        reset();sdk_error=failed ? 89 : 0;
        if(decode){native_mode=99;country.policy=99;second=99;}
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=functions[query](ctx,NULL,arity ? 1 : 0,value);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*result)==(failed || arity || decode));}
        if(arity)assert(!reads);
        if(JS_IsException(*result)) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && (failed || decode)) {
                *result=JS_GetPropertyStr(ctx,*result,"details");*value=JS_GetPropertyStr(ctx,*result,"espCode");
                int32_t n;assert(!JS_ToInt32(ctx,&n,*value) && n==(failed ? 89 : ESP_ERR_INVALID_RESPONSE));
                *value=JS_GetPropertyStr(ctx,*result,"interface");assert(JS_IsNull(*value));
            }
        } else if(query==0) {
            JSCStringBuf b;const char *text=JS_ToCString(ctx,*result,&b);assert(text && !strcmp(text,"station"));
        } else if(query==1) {
            *value=JS_GetPropertyStr(ctx,*result,"code");JSCStringBuf b;const char *text=JS_ToCString(ctx,*value,&b);assert(text && !strcmp(text,"US"));
        } else {
            *value=JS_GetPropertyStr(ctx,*result,"channel");int32_t n;assert(!JS_ToInt32(ctx,&n,*value) && n==(query==2 ? 6 : 1));
            *value=JS_GetPropertyStr(ctx,*result,"channelGeneration");
            if(query==3)assert(JS_IsNull(*value));else assert(!JS_ToInt32(ctx,&n,*value) && n==1);
        }
        JS_PopGCRef(ctx,&value_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks && !critical);free(heap);
    }
    return 0;
}
