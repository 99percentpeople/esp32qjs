
int main(int argc,char **argv) {
    (void)argc;if(!strcmp(argv[1],"reopen"))return run_retained_reopen(atoi(argv[2]),false);
    if(!strcmp(argv[1],"packet-reopen"))return run_retained_reopen(atoi(argv[2]),true);
    if(!strcmp(argv[1],"options"))return run_source_options(atoi(argv[2]));
    if(!strcmp(argv[1],"batch-limit"))return run_small_queue_batch();
    bool packet_mode=argc>3 && atoi(argv[3]);
    int mode=atoi(argv[1]);collect=atoi(argv[2]);int total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;inject=false;
        memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.lifecycle=WIFI_CSI_RUNNING;s_wifi_csi.options.queue_capacity=2;queued=0;
        esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
        assert((s_wifi_csi.resources=esp32_mquickjs_wifi_csi_store_open(&s_wifi_csi_store,2,packet_mode?40:0,&allocator,&allocation_result))!=NULL);
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,true);
        uint8_t packet_bytes[37]={8};
        esp32_mquickjs_wifi_csi_packet_input_t packet_input={packet_bytes,37,37,37};
        if(packet_mode)s_wifi_csi.resources->packet_options=(esp32_mquickjs_wifi_csi_packet_options_t){.mode=ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,.snap_length=40};
        uint8_t bytes[]={1,2,3,4};
        esp32_mquickjs_wifi_csi_metadata_t metadata={.timestamp_us=INT64_C(1)<<45,.radio_generation=17,.channel=6,.rssi=-50,
            .phy_flags=ESP32_MQUICKJS_WIFI_RX_WIRE_FEC_AVAILABLE|ESP32_MQUICKJS_WIFI_RX_WIRE_LDPC,
            .phy=ESP32_MQUICKJS_WIFI_CSI_PHY_HE_SU,.guard_interval_ns=800,.he_ltf_size=4,.dcm_state=1,
            .bandwidth_available=true,.bandwidth_mhz=20,.mcs_available=true,.antenna_available=true,
            .noise_floor_available=true,.stbc_available=true,.channel_estimate_valid_available=true,.channel_estimate_valid=true,
            .layout={.known=true,.schema=ESP32_MQUICKJS_WIFI_CSI_LAYOUT_SCHEMA_LEGACY,.sample_encoding=ESP32_MQUICKJS_WIFI_CSI_SAMPLE_ENCODING_SIGNED_INT8,.byte_length=4,.sample_bits=8,.iq_pair_count=2,.segment_count=1,
                .segments={{.type=ESP32_MQUICKJS_WIFI_CSI_SEGMENT_LLTF,.length_bytes=4,.iq_pair_count=2,.subcarrier_range_count=1,.subcarrier_ranges={{-1,0}},.null_subcarrier_count=1}}}};
        if(mode==8 || mode==12)metadata.layout.iq_pair_count=99;
        if(mode==9 || mode==11){metadata.layout.byte_length=3;metadata.layout.iq_pair_count=1;metadata.layout.trailing_padding_bytes=1;
            metadata.layout.segments[0].length_bytes=2;metadata.layout.segments[0].iq_pair_count=1;metadata.layout.segments[0].subcarrier_ranges[0].end=-1;}
        bool batch_mode=(mode>=4 && mode<=9) || mode==14;
        bool frame_mode=(mode>=1 && mode<=3) || (mode>=10 && !batch_mode);
        bool wire_mode=mode==6 || mode==8 || mode==9 || mode>=10;
        unsigned count=batch_mode ? 2 : 1;
        for(unsigned i=0;i<count;i++)assert(esp32_mquickjs_wifi_csi_callback_publish(s_wifi_csi.resources,&metadata,bytes,(mode==9 || mode==11)?3:4,packet_mode?&packet_input:NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
        JSGCRef owner_ref,result_ref,options_ref;
        JSValue *owner=JS_PushGCRef(ctx,&owner_ref),*result=JS_PushGCRef(ctx,&result_ref),*options=JS_PushGCRef(ctx,&options_ref);
        *owner=*result=*options=JS_UNDEFINED;
        const char *expression="({format: 'esp32qjs-csi/1'})";
        *options=JS_Eval(ctx,expression,strlen(expression),"source-options",JS_EVAL_RETVAL);assert(!JS_IsException(*options));
        JSValue arg=JS_NewInt32(ctx,0);
        if(frame_mode)*owner=js_wifi_csi_session_receive(ctx,owner,0,NULL);
        if(batch_mode && mode!=4)*owner=js_wifi_csi_session_receive_batch(ctx,owner,0,NULL);
        calls=0;fail_at=nth;moved_roots=0;inject=true;
        if(mode==0)*result=js_wifi_csi_session_receive(ctx,owner,0,NULL);
        if(mode==1)*result=packet_mode?js_wifi_csi_frame_packet_bytes(ctx,owner,0,NULL):js_wifi_csi_frame_samples(ctx,owner,0,NULL);
        if(mode==2)*result=packet_mode?js_wifi_csi_frame_copy_packet_bytes(ctx,owner,0,NULL):js_wifi_csi_frame_copy_samples(ctx,owner,0,NULL);
        if(mode==3)*result=packet_mode?js_wifi_csi_frame_packet_source(ctx,owner,0,NULL):js_wifi_csi_frame_sample_source(ctx,owner,0,NULL);
        if(mode==4)*result=js_wifi_csi_session_receive_batch(ctx,owner,0,NULL);
        if(mode==5)*result=packet_mode?js_wifi_csi_batch_packet_bytes(ctx,owner,1,&arg):js_wifi_csi_batch_samples(ctx,owner,1,&arg);
        if(wire_mode && batch_mode)*result=js_wifi_csi_batch_source(ctx,owner,1,options);
        if(wire_mode && frame_mode)*result=js_wifi_csi_frame_source(ctx,owner,1,options);
        if(mode==7)*result=js_wifi_csi_batch_info(ctx,owner,1,&arg);
        inject=false;if(!nth)total=calls;
        if(nth) { if(!JS_IsException(*result) || !JS_HasException(ctx)) {fprintf(stderr,"CSI swallowed failure mode=%d nth=%d\n",mode,nth);return 1;} (void)JS_GetException(ctx); }
        else if(mode==8 || mode==12) { assert(JS_IsException(*result) && JS_HasException(ctx));(void)JS_GetException(ctx); }
        else { assert(!JS_IsException(*result) && !JS_HasException(ctx));if(collect && (mode==0 || mode==4 || mode==7))assert(moved_roots>0); }
        if(!nth && (mode==0 || mode==7)) {
            JSGCRef check_ref;JSValue *value=JS_PushGCRef(ctx,&check_ref);
            *value=mode==0?JS_GetPropertyStr(ctx,*result,"info"):*result;
            assert(!JS_IsException(*value));
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*value,"sourceMac")));
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*value,"rssi")));
            assert(JS_GetPropertyStr(ctx,*value,"rxSequence")== (packet_mode?JS_NewInt32(ctx,0):JS_NULL));
            JSGCRef child_ref;JSValue *child=JS_PushGCRef(ctx,&child_ref);
            *child=JS_GetPropertyStr(ctx,*value,"signal");assert(JS_GetPropertyStr(ctx,*child,"rssi")==JS_NewInt32(ctx,-50));
            *child=JS_GetPropertyStr(ctx,*value,"channel");assert(JS_GetPropertyStr(ctx,*child,"primary")==JS_NewInt32(ctx,6));
            *child=JS_GetPropertyStr(ctx,*value,"addresses");assert(JS_GetClassID(ctx,*child)==JS_CLASS_OBJECT);
            if(!packet_mode)assert(JS_GetPropertyStr(ctx,*child,"source")==JS_NULL);
            *child=JS_GetPropertyStr(ctx,*value,"validity");assert(JS_GetPropertyStr(ctx,*child,"callbackDataValid")==JS_TRUE);
            assert(JS_GetPropertyStr(ctx,*child,"layoutKnown")==JS_TRUE);
            JS_PopGCRef(ctx,&child_ref);
            *value=JS_GetPropertyStr(ctx,*value,"phy");assert(!JS_IsException(*value));
            assert(wifi_csi_string_equals(ctx,JS_GetPropertyStr(ctx,*value,"fecCoding"),"ldpc"));
            assert(JS_GetPropertyStr(ctx,*value,"aggregation")==JS_NULL);
            assert(JS_GetPropertyStr(ctx,*value,"sounding")==JS_NULL);
            assert(JS_GetPropertyStr(ctx,*value,"legacyRate")==JS_NULL);
            JSValue gi=JS_GetPropertyStr(ctx,*value,"guardIntervalNs");
            assert(gi==JS_NewInt32(ctx,800));
            JSValue ltf=JS_GetPropertyStr(ctx,*value,"heLtfSize");assert(ltf==JS_NewInt32(ctx,4));
            assert(JS_GetPropertyStr(ctx,*value,"dcm")==JS_FALSE);
            JS_PopGCRef(ctx,&check_ref);
        }
        /* Close public owners while retained views/sources survive. */
        if(frame_mode)assert(js_wifi_csi_frame_close(ctx,owner,0,NULL)==JS_TRUE);
        if(batch_mode && mode!=4)assert(js_wifi_csi_batch_close(ctx,owner,0,NULL)==JS_TRUE);
        if(!nth && (mode==1 || mode==2 || mode==5)) {
            const uint8_t *data;size_t n;assert(esp32_mquickjs_byte_view_acquire_read(ctx,*result,"test",&data,&n));assert(n==(packet_mode?37:4) && !memcmp(data,packet_mode?packet_bytes:bytes,n));
            esp32_mquickjs_byte_view_release_read(ctx,*result);
        }
        if(!nth && (mode==3 || (wire_mode && mode!=8 && mode!=12))) {
            assert(atomic_load(&s_wifi_csi.resources->counters.leased_frames)>0);
            assert(!esp32_mquickjs_wifi_csi_resources_deinit(s_wifi_csi.resources));
            if(mode>=13){esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);assert(!s_wifi_csi.resources && s_wifi_csi_store.reserved_slots==2);}
            esp32_mquickjs_byte_span_source_t source;JSValue error;
            assert(esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&source,&error));
            if(mode==14 || mode==15) {
                assert(JS_IsException(js_byte_span_source_close(ctx,result,0,NULL)) && JS_HasException(ctx));
                (void)JS_GetException(ctx);
                assert(s_wifi_csi_store.reserved_slots==2);
            }
            esp32_mquickjs_byte_span_t span;size_t size=0;unsigned parts=0;
            while(esp32_mquickjs_byte_span_source_next(ctx,&source,&span)) {
                if(!packet_mode && (mode==9 || mode==11) && parts>0){if(span.length==1)assert(span.data[0]==0);else assert(span.length==3 && !memcmp(span.data,bytes,3));}
                if(wire_mode && parts==0){assert(span.length==32+count*(40+256));assert(!memcmp(span.data,"E32QCSI1",8));}
                if(packet_mode && span.length==37)assert(!memcmp(span.data,packet_bytes,37));
                size+=span.length;parts++;
                if(mode==13)break;
            }
            assert(parts==(mode==3 || mode==13 ? 1 : (mode==9 || mode==11) ? count*(packet_mode?4:2)+1 : count*(packet_mode?3:1)+1));
            assert(size==(mode==3 ? (packet_mode?37:4) : mode==13 ? 32+count*(40+256) : 32+count*(40+256+4+(packet_mode?40:0))));
            esp32_mquickjs_byte_span_source_close(ctx,&source);
            assert(!esp32_mquickjs_open_byte_span_source(ctx,*result,"test",&source,&error));(void)JS_GetException(ctx);
            assert(js_byte_span_source_close(ctx,result,0,NULL)==JS_TRUE);
        }
        JS_PopGCRef(ctx,&options_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&owner_ref);JS_GC(ctx);
        while(queued) {esp32_mquickjs_wifi_csi_event_t event;esp32_mquickjs_event_queue_try_receive(NULL,&event);wifi_csi_update_event(&event,WIFI_CSI_EVENT_DISCARD);}
        assert(!s_wifi_csi.resources || !atomic_load(&s_wifi_csi.resources->counters.leased_frames));
        esp32_mquickjs_wifi_csi_resources_set_accepting(s_wifi_csi.resources,false);s_wifi_csi.lifecycle=WIFI_CSI_CLOSED;wifi_csi_maybe_destroy_resources(&s_wifi_csi);
        assert(!s_wifi_csi.resources && !s_wifi_csi_store.reserved_slots && root_count==0 && native_live==0);
        JS_FreeContext(ctx);free(heap);
    }
    printf("CSI mode=%d gc=%d boundaries=%d\n",mode,collect,total);
}
