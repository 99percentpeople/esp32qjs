
static int run_retained_reopen(bool moving, bool packet_mode) {
    void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
    memset(&s_wifi_csi,0,sizeof(s_wifi_csi));queued=0;collect=moving;inject=true;fail_at=0;
    esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
    s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,packet_mode?40:0,&allocator,&allocation_result);assert(s_wifi_csi.resources);
    s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;s_wifi_csi.options.queue_capacity=2;
    uint8_t packet_bytes[37]={8};
    esp32_mquickjs_wifi_csi_packet_input_t packet_input={packet_bytes,37,37,37};
    if(packet_mode)s_wifi_csi.resources->packet_options=(esp32_mquickjs_wifi_csi_packet_options_t){.mode=ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,.snap_length=40};
    unsigned old_generation=s_wifi_csi.resources->generation;
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
    uint8_t bytes[]={7,8,9,10};esp32_mquickjs_wifi_csi_metadata_t metadata={.timestamp_us=55,.layout={.byte_length=4,.segment_count=1,.segments={{.length_bytes=4}}}};
    assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,4,packet_mode?&packet_input:NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    JSGCRef frame_ref,view_ref,source_ref;
    JSValue *frame=JS_PushGCRef(ctx,&frame_ref),*view=JS_PushGCRef(ctx,&view_ref),*source=JS_PushGCRef(ctx,&source_ref);
    *frame=js_wifi_csi_session_receive(ctx,frame,0,NULL);assert(!JS_IsException(*frame));
    *view=packet_mode?js_wifi_csi_frame_packet_bytes(ctx,frame,0,NULL):js_wifi_csi_frame_samples(ctx,frame,0,NULL);assert(!JS_IsException(*view));
    *source=packet_mode?js_wifi_csi_frame_packet_source(ctx,frame,0,NULL):js_wifi_csi_frame_sample_source(ctx,frame,0,NULL);assert(!JS_IsException(*source));
    esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);
    s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);assert(!s_wifi_csi.resources);
    s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,0,&allocator,&allocation_result);assert(s_wifi_csi.resources);
    s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;assert(s_wifi_csi.resources->generation!=old_generation && s_wifi_csi_store.reserved_slots==4);
    /* Public methods on the old Frame must resolve its own generation. */
    const uint8_t *data;size_t length;assert(esp32_mquickjs_byte_view_acquire_read(ctx,*view,"old",&data,&length));
    assert(length==(packet_mode?37:4) && !memcmp(data,packet_mode?packet_bytes:bytes,length));esp32_mquickjs_byte_view_release_read(ctx,*view);
    assert(js_wifi_csi_frame_close(ctx,frame,0,NULL)==JS_TRUE);
    assert(js_byte_view_close(ctx,view,0,NULL)==JS_TRUE);
    esp32_mquickjs_byte_span_source_t stream;JSValue error;
    assert(esp32_mquickjs_open_byte_span_source(ctx,*source,"old",&stream,&error));
    /* Source close preserves the generic active-read busy contract. The old
     * pool remains readable across the rejected close and a new generation. */
    assert(JS_IsException(js_byte_span_source_close(ctx,source,0,NULL)) && JS_HasException(ctx));
    (void)JS_GetException(ctx);
    esp32_mquickjs_byte_span_t span;assert(esp32_mquickjs_byte_span_source_next(ctx,&stream,&span));
    assert(span.length==(packet_mode?37:4) && !memcmp(span.data,packet_mode?packet_bytes:bytes,span.length));
    assert(s_wifi_csi_store.reserved_slots==4);
    esp32_mquickjs_byte_span_source_close(ctx,&stream);
    assert(js_byte_span_source_close(ctx,source,0,NULL)==JS_TRUE);
    assert(js_byte_span_source_close(ctx,source,0,NULL)==JS_TRUE);
    assert(!esp32_mquickjs_wifi_csi_store_acquire(&s_wifi_csi_store,old_generation));
    assert(s_wifi_csi_store.reserved_slots==2 && s_wifi_csi.resources);
    s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
    JS_PopGCRef(ctx,&source_ref);JS_PopGCRef(ctx,&view_ref);JS_PopGCRef(ctx,&frame_ref);JS_GC(ctx);
    inject=collect=false;assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && !native_live && !root_count);
    JS_FreeContext(ctx);free(heap);return 0;
}
