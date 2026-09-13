
static int run_source_options(bool batch_mode) {
    const char *cases[]={
        "owner.source()", "owner.source(null)", "owner.source({})",
        "owner.source({format: undefined})", "owner.source({format: 'esp32qjs-csi/1\\x00'})",
        "owner.source({format: 'esp32qjs-csi/1', extra: 1})",
        "owner.source({get format() { owner.close(); gc(); return 'esp32qjs-csi/1'; }})",
        "(function () { try { owner.source({get format() { throw 12345; }}); } catch (e) { return e === 12345; } return false; })()",
        "owner.source({get format() { gc(); return 'esp32qjs-csi/1'; }})",
        "(function () { var reads=0; var s=owner.source({get format() { reads++; return 'esp32qjs-csi/1'; }}); if (reads!==1) throw new Error('repeated getter'); return s; })()"
    };
    for(unsigned index=0;index<sizeof(cases)/sizeof(cases[0]);index++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);
        test_ctx=ctx;inject=false;memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;s_wifi_csi.options.queue_capacity=2;queued=0;
        esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
        assert((s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,0,&allocator,&allocation_result))!=NULL);
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
        uint8_t bytes[]={1,2,3,4};
        esp32_mquickjs_wifi_csi_metadata_t metadata={.timestamp_us=123456,.radio_generation=17,.channel=6,
            .layout={.byte_length=4,.segment_count=1,.segments={{.length_bytes=4}}}};
        assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,4,NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
        JSGCRef owner_ref,result_ref;
        JSValue *owner=JS_PushGCRef(ctx,&owner_ref),*result=JS_PushGCRef(ctx,&result_ref);*owner=*result=JS_UNDEFINED;
        *owner=batch_mode?js_wifi_csi_session_receive_batch(ctx,owner,0,NULL):js_wifi_csi_session_receive(ctx,owner,0,NULL);
        assert(!JS_IsException(*owner));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"owner",*owner)));
        *result=JS_Eval(ctx,cases[index],strlen(cases[index]),"source-options",JS_EVAL_RETVAL);
        if(index<=6) {
            assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx);
            assert(atomic_load(&s_wifi_csi.resources->counters.leased_frames)==(index==6?0:1));
            if(index==6)assert(JS_GetOpaque(ctx,*owner)==NULL);
        } else if(index==7)assert(*result==JS_TRUE && !JS_HasException(ctx));
        else {
            assert(!JS_IsException(*result) && !JS_HasException(ctx));
            esp32_mquickjs_byte_span_source_t stream;JSValue error;
            assert(esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&stream,&error));
            esp32_mquickjs_byte_span_t span;size_t total=0;
            while(esp32_mquickjs_byte_span_source_next(ctx,&stream,&span))total+=span.length;
            assert(total==332);
            esp32_mquickjs_byte_span_source_close(ctx,&stream);
            assert(js_byte_span_source_close(ctx,result,0,NULL)==JS_TRUE);
        }
        assert((batch_mode?js_wifi_csi_batch_close(ctx,owner,0,NULL):js_wifi_csi_frame_close(ctx,owner,0,NULL))==JS_TRUE);
        JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&owner_ref);JS_GC(ctx);
        assert(!s_wifi_csi.resources || !atomic_load(&s_wifi_csi.resources->counters.leased_frames));
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
        assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && root_count==0 && native_live==0);
        JS_FreeContext(ctx);free(heap);
    }
    return 0;
}
