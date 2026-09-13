
int main(int argc,char **argv) {
    (void)argc;int mode=atoi(argv[1]);collect=atoi(argv[2]);driver_failure=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        calls=0;fail_at=nth;moved_roots=0;
        espnow_peer_slot_t peer={.rate_config={.configured=true,.mcs=7},.channel=0,.encrypted=true};
        espnow_session_t session={.broadcast_rate_config=peer.rate_config,.tx_queue_capacity=4,.channel=6,.channel_generation=1};
        JSGCRef ref;JSValue *result=JS_PushGCRef(ctx,&ref);inject=true;
        if(mode==0)*result=wifi_make_status_object(ctx);
        if(mode==4)*result=js_wifi_driver_status(ctx,NULL,0,NULL);
        if(mode==1)*result=wifi_make_scan_results_array(ctx,32);
        if(mode==2)*result=espnow_peer_status_to_js(ctx,&peer);
        if(mode==3)*result=espnow_status_to_js(ctx,&session);
        inject=false;if(!nth)total=calls;
        if(nth || driver_failure) { assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx); }
        else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));if(collect)assert(moved_roots>0);
            if(mode==0 || mode==4) {
                if(mode==0) {
                    int32_t channel;
                    assert(!JS_ToInt32(ctx,&channel,JS_GetPropertyStr(ctx,*result,"channel")) && channel==6);
                    assert(!JS_ToInt32(ctx,&channel,JS_GetPropertyStr(ctx,*result,"aid")) && channel==41);
                    assert(!JS_ToInt32(ctx,&channel,JS_GetPropertyStr(ctx,*result,"rssi")) && channel==-50);
                    *result=JS_GetPropertyStr(ctx,*result,"radio");
                }
                JSValue configuration=JS_GetPropertyStr(ctx,*result,"configuration");
                assert(JS_GetPropertyStr(ctx,configuration,"rollbackComplete")==JS_TRUE);
                int32_t mask;assert(!JS_ToInt32(ctx,&mask,JS_GetPropertyStr(ctx,*result,"eventStoppedMask")) && mask==1);
                assert(!JS_ToInt32(ctx,&mask,JS_GetPropertyStr(ctx,*result,"eventSeenMask")) && mask==0);
                JSCStringBuf phase_buf;const char *phase=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*result,"eventPhase"),&phase_buf);
                assert(phase && !strcmp(phase,"restart"));
                *result=JS_GetPropertyStr(ctx,*result,"policies");
                *result=JS_GetPropertyStr(ctx,*result,"dynamicCarrierSense");
                assert(JS_IsNull(JS_GetPropertyStr(ctx,*result,"value")));
                assert(JS_GetPropertyStr(ctx,*result,"lastAcceptedValue")==JS_FALSE);
                assert(JS_GetPropertyStr(ctx,*result,"uncertain")==JS_TRUE);
                int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"revision")) && n==2);
            }
            if(mode==1) { int32_t n;assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,*result,"length")) && n==2); }
        }
        JS_PopGCRef(ctx,&ref);JS_GC(ctx);assert(root_count==0 && native_live==0);JS_FreeContext(ctx);free(heap);
    }
    printf("status mode=%d gc=%d boundaries=%d\n",mode,collect,total);
}
