int main(int argc,char **argv) {
    assert(argc==2);int scenario=atoi(argv[1]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);
        esp32_mquickjs_future_driver_state_t state={.kind=WIFI_FUTURE_SCAN,.generation=7,.started=scenario!=2,.scan_max_records=1};
        stop_calls=cancel_calls=0;stop_error=scenario==3?-2:0;
        driver_failure=scenario==4?1:scenario==5?2:0;
        terminal_queued=scenario==6 || scenario==7 || scenario==9;terminal_status=scenario==7?1:0;
        terminal_generation=scenario==9?6:7;
        fixture_scan_ap_count=scenario==8?0:2;ap_reads=0;stopped_or_complete=scenario==0;
        calls=0;fail_at=nth;inject=true;collect=true;
        if(scenario==0)*result=wifi_future_finish(ctx,&state);
        else {
#if HAS_SCAN_TIMEOUT
            *result=wifi_scan_future_on_timeout(ctx,&state,10);
#else
            *result=JS_ThrowInternalError(ctx,"generic Future timeout");
#endif
        }
        inject=false;collect=false;if(!nth)total=calls;
        bool error=scenario==3 || scenario==4 || scenario==5 || scenario==7;
        assert(JS_IsException(*result)==(error || nth!=0));
        if(JS_IsException(*result)) {assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        else {
            bool complete=scenario==0 || scenario==6;
            assert(JS_GetPropertyStr(ctx,*result,"complete")==JS_NewBool(complete));
            assert(JS_GetPropertyStr(ctx,*result,"timedOut")==JS_NewBool(!complete));
            *result=JS_GetPropertyStr(ctx,*result,"records");assert(JS_IsArray(ctx,*result));
            assert(JS_GetPropertyStr(ctx,*result,"length")==JS_NewInt32(ctx,scenario==2 || scenario==8?0:1));
            assert(stop_calls==(scenario==1 || scenario==8 || scenario==9?1:0));
            assert(cancel_calls==(scenario==2?0:1));
        }
        if(scenario==2 || scenario==3)assert(ap_reads==0);
        JS_PopGCRef(ctx,&ref);JS_FreeContext(ctx);free(heap);assert(!root_count && !native_live);
    }
}
