
static int run_small_queue_batch(void) {
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;
    s_wifi_csi.options.queue_capacity=1;queued=0;
    esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
    assert((s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,1,0,&allocator,&allocation_result))!=NULL);
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
    uint8_t bytes[]={1,2,3,4};
    esp32_mquickjs_wifi_csi_metadata_t metadata={.channel=6,.timestamp_us=1,
        .layout={.byte_length=4,.sample_encoding=ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8}};
    assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,4,NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    JSGCRef result_ref,options_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*options=JS_PushGCRef(ctx,&options_ref);
    const char *expression="({maximumFrames:2})";
    *options=JS_Eval(ctx,expression,strlen(expression),"oversized-batch",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
    *result=js_wifi_csi_session_receive_batch(ctx,result,1,options);
    assert(JS_IsException(*result)&&JS_HasException(ctx));JS_GetException(ctx);
    assert(queued==1 && atomic_load(&s_wifi_csi.resources->counters.leased_frames)==1);
    /* The omitted maximum is clipped to queue=1 and the queued owner survives
     * the previous validation failure. Call the actual batch adapter. */
    *result=js_wifi_csi_session_receive_batch(ctx,result,0,NULL);
    assert(!JS_IsException(*result));wifi_csi_batch_ref_t *batch=JS_GetOpaque(ctx,*result);
    assert(batch && batch->frame_count==1 && !queued);
    assert(js_wifi_csi_batch_close(ctx,result,0,NULL)==JS_TRUE);
    JS_PopGCRef(ctx,&options_ref);JS_PopGCRef(ctx,&result_ref);JS_GC(ctx);
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
    assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && !root_count && !native_live);
    JS_FreeContext(ctx);free(heap);return 0;
}
