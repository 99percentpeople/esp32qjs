
int main(int argc,char **argv) {
    assert(argc==3);unsigned query=atoi(argv[1]);int total=1;
    JSValue (*functions[])(JSContext *,JSValue *,int,JSValue *)={
        js_wifi_driver_get_band,js_wifi_driver_get_band_mode,js_wifi_driver_get_power_save,
        js_wifi_driver_get_tx_power,js_wifi_driver_get_rssi,js_wifi_driver_get_aid,
        js_wifi_driver_get_negotiated_phy,js_wifi_driver_get_tsf_time,js_wifi_driver_get_inactive_time};
    bool failed=!strcmp(argv[2],"sdk-error"),arity=!strcmp(argv[2],"arity");
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,input_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*input=JS_PushGCRef(ctx,&input_ref);
        *input=JS_NewString(ctx,"station");reads=0;calls=0;fail_at=nth;collect=true;inject=true;
        sdk_error=failed ? 89 : 0;tsf=failed ? -1 : INT64_C(9007199254740991);
        bool by_interface=query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME || query==ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME;
        *result=functions[query](ctx,NULL,arity ? 2 : by_interface ? 1 : 0,input);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*result)==(failed || arity));}
        if(arity)assert(reads==0);
        if(JS_IsException(*result)) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && failed) {
                *result=JS_GetPropertyStr(ctx,*result,"details");
                *input=JS_GetPropertyStr(ctx,*result,"espCode");int32_t n;assert(!JS_ToInt32(ctx,&n,*input));
                assert(n==(query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME ? ESP_ERR_INVALID_RESPONSE : 89));
                *input=JS_GetPropertyStr(ctx,*result,"interface");
                if(query<=ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER)assert(JS_IsNull(*input));
                else assert(JS_IsString(ctx,*input));
            }
        } else if(query==ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER || query==ESP32_MQUICKJS_WIFI_DRIVER_RSSI ||
                  query==ESP32_MQUICKJS_WIFI_DRIVER_AID || query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME ||
                  query==ESP32_MQUICKJS_WIFI_DRIVER_INACTIVE_TIME) {
            double n;assert(!JS_ToNumber(ctx,&n,*result));
            assert(n==(query==ESP32_MQUICKJS_WIFI_DRIVER_TX_POWER ? 18.25 : query==ESP32_MQUICKJS_WIFI_DRIVER_RSSI ? -63 :
                query==ESP32_MQUICKJS_WIFI_DRIVER_AID ? 17 : query==ESP32_MQUICKJS_WIFI_DRIVER_TSF_TIME ? 9007199254740991.0 : 300));
        } else assert(JS_IsString(ctx,*result));
        JS_PopGCRef(ctx,&input_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks);free(heap);
    }
    return 0;
}
