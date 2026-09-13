
int main(int argc,char **argv) {
    assert(argc==4);unsigned expected=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"stop",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        if(atoi(argv[3])==2){
            esp32_mquickjs_future_driver_state_t *state=NULL;
            assert(wifi_disconnect_future_prepare(ctx,NULL,0,NULL,&state));
            assert(state->timeout_ms==ESP32_MQUICKJS_WIFI_DEFAULT_TIMEOUT_MS);
            esp32_mquickjs_memory_payload_free(state);state=NULL;
            assert(!wifi_disconnect_future_prepare(ctx,NULL,2,&ref,&state));
            assert(!state && JS_HasException(ctx));JS_GetException(ctx);
            assert(!native_live);
        }
        uint32_t timeout=0xdeadbeef;
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok;
        if(atoi(argv[3])==2){
            esp32_mquickjs_future_driver_state_t *state=NULL;
            ok=wifi_disconnect_future_prepare(ctx,NULL,1,&ref,&state);
            if(ok){assert(state && state->kind==WIFI_FUTURE_DISCONNECT);timeout=state->timeout_ms;esp32_mquickjs_memory_payload_free(state);}
            else {assert(!state);timeout=0;}
        } else {
            ok=atoi(argv[3]) ? esp32_mquickjs_wifi_capture_stop_ap_timeout(ctx,*root,&timeout) : esp32_mquickjs_wifi_capture_stop(ctx,*root,&timeout);
        }
        if(!nth){total=calls;assert(ok==(expected!=0));}
        if(nth)assert(!ok);
        if(ok)assert(timeout==expected && !JS_HasException(ctx));
        else { assert(timeout==0 && JS_HasException(ctx));JS_GetException(ctx); }
        assert(!native_live);inject=false;collect=false;JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);test_ctx=NULL;
    }
}
