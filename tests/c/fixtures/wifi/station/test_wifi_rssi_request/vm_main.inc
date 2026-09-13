
static void number(JSContext *ctx,JSValue *root,const char *key,int32_t expected) {
    JSValue value=JS_GetPropertyStr(ctx,*root,key);int32_t actual;
    assert(!JS_IsException(value) && !JS_ToInt32(ctx,&actual,value) && actual==expected);
}
static void boolean(JSContext *ctx,JSValue *root,const char *key,bool expected) {
    assert(JS_GetPropertyStr(ctx,*root,key)==(expected ? JS_TRUE : JS_FALSE));
}
int main(void) {
    for(unsigned scenario=0;scenario<5;++scenario) {
        int total=1;
        for(int nth=0;nth<=total;++nth) {
            void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
            JSGCRef root_ref;JSValue *root=JS_PushGCRef(ctx,&root_ref);
            reset();int32_t actual;
            if(scenario) {
                if(scenario==2)native_fail_at=1;
                int err=esp32_mquickjs_wifi_apply_connection_control(
                    ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result);
                assert(err==(scenario==2 ? 77 : ESP_OK));
            }
            if(scenario==3)s_radio.driver_owned=false;
            if(scenario==4){++s_radio.generation;s_rssi_request.revision=UINT32_MAX;}
            esp32_mquickjs_wifi_rssi_request_t saved=s_rssi_request;
            unsigned before=native_calls;
            calls=0;fail_at=nth;collect=true;inject=true;
            *root=esp32_mquickjs_wifi_rssi_request_to_js(ctx);
            inject=false;collect=false;
            if(!nth){total=calls;assert(!JS_IsException(*root));}
            if(JS_IsException(*root)) {
                assert(JS_HasException(ctx));JS_GetException(ctx);
            } else {
                boolean(ctx,root,"accepted",scenario!=0 && scenario!=2);
                boolean(ctx,root,"generationActive",scenario==1 || scenario==2);
                boolean(ctx,root,"identityExhausted",scenario==4);
                if(!scenario) {
                    number(ctx,root,"revision",0);
                    assert(JS_GetPropertyStr(ctx,*root,"generation")==JS_NULL);
                    assert(JS_GetPropertyStr(ctx,*root,"requestedDbm")==JS_NULL);
                    assert(JS_GetPropertyStr(ctx,*root,"espCode")==JS_NULL);
                    assert(JS_GetPropertyStr(ctx,*root,"espName")==JS_NULL);
                } else {
                    number(ctx,root,"generation",7);number(ctx,root,"requestedDbm",-75);
                    number(ctx,root,"espCode",scenario==2 ? 77 : ESP_OK);
                }
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*root,"armed")));
                assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*root,"value")));
            }
            assert(native_calls==before && !memcmp(&saved,&s_rssi_request,sizeof(saved)));
            JS_PopGCRef(ctx,&root_ref);JS_FreeContext(ctx);
            assert(!root_count && !native_live && !locks && !critical && !helper_locks);free(heap);
        }
    }
    return 0;
}
