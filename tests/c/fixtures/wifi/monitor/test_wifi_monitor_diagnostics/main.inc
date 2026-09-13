
static void check(JSContext *ctx,const char *expression) {
    JSValue value=JS_Eval(ctx,expression,strlen(expression),"diagnostics",JS_EVAL_RETVAL);
    assert(!JS_IsException(value) && value==JS_TRUE);
}
int main(int argc,char **argv) {
    assert(argc==2);int scenario=atoi(argv[1]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        pool_snapshot=(esp32_mquickjs_wifi_monitor_snapshot_t){.free_slots=7,.identity_exhausted=true,
            .counters={.callbacks=99,.accepted=42,.dropped_queue_full=3,.leased_frames=9}};
        for(unsigned i=0;i<ESP32_MQUICKJS_WIFI_MONITOR_FILTER_COUNTERS;i++)pool_snapshot.counters.filtered[i]=100+i;
        queue_snapshot=(esp32_mquickjs_event_queue_stats_t){.open=true,.queued=2,.capacity=16,.receiver_pending=true};
        queue_available=scenario!=4;pool_reads=queue_reads=0;
        JSGCRef r;JSValue *root=push_root(ctx,&r);*root=JS_UNDEFINED;
        calls=0;fail_at=nth;inject=collect=true;
        esp32_mquickjs_event_queue_t queue={0};
        if(scenario==0)*root=js_wifi_monitor_capabilities(ctx,NULL,0,NULL);
        else if(scenario==1)*root=js_wifi_monitor_session_stats(ctx,root,0,NULL);
        else *root=monitor_queue_status(ctx,scenario==3?NULL:&queue);
        inject=collect=false;
        if(nth==0) {total=calls;assert(!JS_IsException(*root));}
        else {assert(JS_IsException(*root));assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        assert(root_count==1 && !native_live);
        assert(pool_reads==(scenario==1?1U:0U));
        assert(queue_reads==((scenario==2 || scenario==4)?1U:0U));
        if(nth==0) {
            if(scenario==2) {
                /* Flat queue fields are immediate numbers/bools. The one
                 * object allocation precedes all live result roots, so there
                 * is no allocating child constructor to force an internal
                 * move. Collect the returned, rooted object across the real
                 * allocation hole instead of inventing numeric allocations. */
                JSValue previous=*root;
                JS_GC(ctx);
                assert(*root!=previous);
                moved_roots++;
            }
            assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"value",*root)));
            if(scenario==0) {
                check(ctx,"value.apiVersion==='wifi-monitor/1' && value.target==='" CONFIG_IDF_TARGET "' && value.idfVersion==='fixture-sdk' && value.stability==='candidate'");
                check(ctx,"value.supports.configure && value.supports.fixedChannel && value.supports.sourceMacFilter && value.supports.destinationMacFilter && value.supports.bssidFilter && value.supports.nativeRateLimit && value.supports.nativeDecimation && value.supports.receiveBatch && value.supports.wireSource && value.supports.hostPcapngConverter");
                check(ctx,"value.limits.maxSessions===8 && value.limits.maxPoolCapacity===128 && value.limits.maxQueueCapacity===128 && value.limits.maxSnapLength===16384 && value.limits.maxBatchFrames===128 && value.limits.maxMacsPerRole===8");
                check(ctx,"value.frameTypes.join(',')==='management,control,data,misc' && value.timestampAccuracy==='callback-time' && value.configure===undefined && value.maxSnapLength===undefined");
                assert(JS_IsException(js_wifi_monitor_capabilities(ctx,NULL,1,NULL)));
                (void)JS_GetException(ctx);
            } else if(scenario==1) {
                check(ctx,"value.callbacks===99 && value.accepted===42 && value.droppedQueueFull===3 && value.leasedFrames===9 && value.freeSlots===7 && value.identityExhausted");
                check(ctx,"value.filtered.invalidConfig===101 && value.filtered.invalidCallback===102 && value.filtered.invalidHeader===103 && value.filtered.rxError===104 && value.filtered.type===105 && value.filtered.subtype===106 && value.filtered.mac===107 && value.filtered.rssi===108 && value.filtered.decimation===109 && value.filtered.rate===110 && value.filtered[0]===undefined");
                assert(JS_IsException(js_wifi_monitor_session_stats(ctx,root,1,NULL)));
                (void)JS_GetException(ctx);assert(pool_reads==1);
            } else if(scenario==2) check(ctx,"value.open && value.queued===2 && value.capacity===16 && value.receiverPending");
            else check(ctx,"value===null");
        }
        pop_root(ctx,&r);assert(!root_count);JS_FreeContext(ctx);free(heap);
    }
    if(scenario<3)assert(moved_roots>0);
}
