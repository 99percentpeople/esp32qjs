
int main(void){
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    assert(JS_IsException(js_wifi_diagnostics_reset_counters(ctx,NULL,1,NULL)));JS_GetException(ctx);assert(!providers);
    active=false;assert(JS_IsException(js_wifi_diagnostics_reset_counters(ctx,NULL,0,NULL)));JS_GetException(ctx);assert(!providers);active=true;
    inject=true;calls=0;fail_at=1;
    assert(JS_IsUndefined(js_wifi_diagnostics_reset_counters(ctx,NULL,0,NULL))&&!calls);
    inject=false;assert(providers==5+CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI);
    assert(s_diagnostics_reset.count==1&&s_diagnostics_reset.started_us==1&&s_diagnostics_reset.finished_us==2);
    assert(s_diagnostics_reset.unavailable_monitor_generations==2);
    assert(s_diagnostics_reset.unavailable_csi_generations==3*CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI);
    JSGCRef ref;JSValue *r=JS_PushGCRef(ctx,&ref);int total=0;
    for(int nth=0;nth<=total;nth++){
        inject=true;collect=true;calls=0;fail_at=nth;*r=wifi_diagnostics_reset_to_js(ctx,&s_diagnostics_reset);
        inject=false;collect=false;
        if(!nth){total=calls;assert(!JS_IsException(*r));}else {assert(JS_IsException(*r));JS_GetException(ctx);}
        assert(providers==5+CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI&&s_diagnostics_reset.count==1);
    }
    JS_PopGCRef(ctx,&ref);assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
}
