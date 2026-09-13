
typedef esp32_mquickjs_wifi_monitor_session_t session_t;
static JSContext ctx;
static esp32_mquickjs_runtime_t runtime_a,runtime_b;
static esp32_mquickjs_wifi_monitor_capture_options_t options={.filter={.type_mask=4,.sample_every=1,.valid_only=true}};
static JSValue frame(JSContext *context,const esp32_mquickjs_wifi_monitor_event_t *event,session_t *session) {
    (void)context;(void)event;(void)session;return JS_EXCEPTION;
}
static unsigned owners(void) {
    unsigned count=0;for(unsigned i=0;i<ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS;i++)if(s_monitor_sessions[i])++count;return count;
}
static session_t *create(esp32_mquickjs_runtime_t *runtime) {
    session_t *session=NULL;
    assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,runtime,&options,2,24,false,2,frame,&session)==17);
    assert(session && atomic_load(&session->references)==3 && session->runtime==runtime && session->cleanup_hold);
    return session;
}
static void dispose_saved_queue(esp32_mquickjs_event_queue_t *queue) {
    js_opaque=queue;assert(esp32_mquickjs_event_queue_dispose(&ctx,17));
}
static void finish(session_t *session) {
    esp32_mquickjs_event_queue_t *queue=session->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    if(queue)dispose_saved_queue(queue);
    esp32_mquickjs_wifi_monitor_session_release(session);
}
static void failures(void) {
    session_t *session=NULL;
    fail_poller=true;
    assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,&runtime_a,&options,2,24,false,2,frame,&session)==JS_EXCEPTION);
    fail_poller=false;assert(!session && !owners());
    for(unsigned nth=1;nth<=4;nth++) {
        sdk_allocation_calls=0;fail_sdk_allocation_at=nth;
        assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,&runtime_a,&options,2,24,false,2,frame,&session)==JS_EXCEPTION);
        fail_sdk_allocation_at=0;
        assert(!session && !owners() && !roots && !radio_acquires);
    }
    bool *flags[]={&fail_resources,&fail_object,&fail_lookup,&fail_bind};
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        *flags[i]=true;
        assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,&runtime_a,&options,2,24,false,2,frame,&session)==JS_EXCEPTION);
        *flags[i]=false;assert(!session && !owners() && !roots && !radio_acquires);
    }
}
static bool busy(void *opaque) { assert(opaque);return false; }
static void delayed_cleanup(void) {
    session_t *session=create(&runtime_a);
    esp32_mquickjs_event_queue_t *queue=session->bridge.queue;
    current_task=2;assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_ERR_INVALID_STATE && !radio_acquires);
    current_task=1;assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);
    for(unsigned i=0;i<ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY;i++)
        assert(esp32_mquickjs_reaper_register(&registry,busy,(void *)(uintptr_t)(i+1)));
    hold_radio_rx=true;
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_ERR_TIMEOUT);
    assert(session->reaper_full && !session->reaper_registered && session->cleanup_hold);
    dispose_saved_queue(queue);esp32_mquickjs_wifi_monitor_session_release(session); /* Public JS owner gone. */
    assert(owners()==1 && session->runtime==&runtime_a && atomic_load(&session->references)==2);
    assert(!esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(&runtime_a));
    assert(esp32_mquickjs_reaper_unregister(&registry,busy,(void *)1));
    assert(registered_poller(&ctx,&runtime_a,NULL));
    assert(session->reaper_registered && !session->reaper_full);
    hold_radio_rx=false;
    for(unsigned i=0;i<ESP32_MQUICKJS_REAPER_CAPACITY;i++)
        (void)esp32_mquickjs_reaper_poll(&registry,4,NULL);
    assert(!owners() && radio_releases==1);
    for(unsigned i=1;i<ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY;i++)
        assert(esp32_mquickjs_reaper_unregister(&registry,busy,(void *)(uintptr_t)(i+1)));
    assert(!esp32_mquickjs_reaper_pending(&registry));
}
static void retirement_and_capacity(void) {
    session_t *sessions[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
    for(unsigned i=0;i<ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS;i++)sessions[i]=create(&runtime_a);
    session_t *extra=NULL;
    assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,&runtime_a,&options,2,24,false,2,frame,&extra)==JS_EXCEPTION && !extra);
    /* CLOSED controls still count while a native owner retains them. */
    for(unsigned i=0;i<ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS;i++) {
        esp32_mquickjs_event_queue_t *queue=sessions[i]->bridge.queue;
        assert(esp32_mquickjs_wifi_monitor_session_close(sessions[i])==ESP_OK);
        dispose_saved_queue(queue);
        assert(atomic_load(&sessions[i]->references)==1 && !sessions[i]->runtime);
    }
    assert(owners()==ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS);
    assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,&runtime_a,&options,2,24,false,2,frame,&extra)==JS_EXCEPTION && !extra);
    uint32_t previous=sessions[0]->generation;
    esp32_mquickjs_wifi_monitor_session_release(sessions[0]);
    extra=create(&runtime_b);assert(extra->generation>previous);finish(extra);
    for(unsigned i=1;i<ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS;i++)esp32_mquickjs_wifi_monitor_session_release(sessions[i]);
    assert(!owners());
}
static void teardown_isolation(void) {
    session_t *a=create(&runtime_a),*b=create(&runtime_b);
    esp32_mquickjs_event_queue_t *qa=a->bridge.queue,*qb=b->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(a)==ESP_OK && esp32_mquickjs_wifi_monitor_session_start(b)==ESP_OK);
    assert(esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(&runtime_a));
    assert(atomic_load(&a->closed) && !a->runtime && b->runtime==&runtime_b && !atomic_load(&b->closed));
    dispose_saved_queue(qa);esp32_mquickjs_wifi_monitor_session_release(a);
    assert(esp32_mquickjs_prepare_wifi_monitor_runtime_destroy(&runtime_b));
    dispose_saved_queue(qb);esp32_mquickjs_wifi_monitor_session_release(b);assert(!owners());
}
static void publish_one(session_t *session) {
    uint8_t bytes[24]={8};
    esp32_mquickjs_wifi_rx_target_view_t view={.status=ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET,
        .bytes=bytes,.readable_length=sizeof(bytes),.metadata={.available=true}};
    assert(esp32_mquickjs_wifi_monitor_publish(&session->resources,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,10)==ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED);
}
static void future_and_view_retention(void) {
    session_t *session=create(&runtime_a);
    esp32_mquickjs_event_queue_t *queue=session->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);publish_one(session);
    esp32_mquickjs_future_driver_state_t *future=calloc(1,sizeof(*future));assert(future);
    future->event=malloc(sizeof(pending));assert(future->event);memcpy(future->event,&pending,sizeof(pending));has_event=false;
    future->queue=queue;future->received=true;future->native_queue_retained=true;
    assert(esp32_mquickjs_event_queue_retain(queue));
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    dispose_saved_queue(queue);esp32_mquickjs_wifi_monitor_session_release(session);
    assert(owners()==1 && !session->runtime && atomic_load(&session->references)==1);
    assert(event_queue_future_finish(&ctx,future)==JS_EXCEPTION); /* Real bridge closes failed Frame root. */
    event_queue_future_destroy(future);assert(!owners());

    session=create(&runtime_a);queue=session->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);publish_one(session);
    esp32_mquickjs_wifi_monitor_event_t event=pending;has_event=false;
    assert(esp32_mquickjs_wifi_monitor_take_event(&session->resources,&event));
    assert(esp32_mquickjs_wifi_monitor_session_retain(session)); /* Frame owner */
    esp32_mquickjs_wifi_monitor_ref_t ref={0};
    assert(esp32_mquickjs_wifi_monitor_retain_frame(&session->resources,&event,&ref));
    assert(esp32_mquickjs_wifi_monitor_session_retain(session)); /* View owner */
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    dispose_saved_queue(queue); /* Keep the JS Session control alive for status. */
    assert(owners()==1 && atomic_load(&session->references)==3);
    esp32_mquickjs_wifi_monitor_diagnostic_t diagnostics[ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS];
    uint32_t unavailable=99;
    assert(esp32_mquickjs_wifi_monitor_diagnostics_snapshot(diagnostics,&unavailable)==1 && !unavailable);
    assert(diagnostics[0].generation==session->generation && diagnostics[0].closed);
    assert(diagnostics[0].resources.allocated_bytes>0 && diagnostics[0].resources.counters.leased_frames==1);
    assert(atomic_load(&session->references)==3); /* Observation owns no lasting reference. */
    atomic_store(&session->references,UINT32_MAX);
    assert(!esp32_mquickjs_wifi_monitor_diagnostics_snapshot(diagnostics,&unavailable) && unavailable==1);
    assert(atomic_load(&session->references)==UINT32_MAX); /* Failed retain never releases another owner. */
    atomic_store(&session->references,3);
    assert(esp32_mquickjs_wifi_monitor_close_frame(&session->resources,&event));
    esp32_mquickjs_wifi_monitor_session_release(session);
    const uint8_t *data;size_t length;
    assert(esp32_mquickjs_wifi_monitor_ref_data(&ref,&data,&length) && length==24 && data[0]==8);
    assert(esp32_mquickjs_wifi_monitor_release_ref(&ref));
    esp32_mquickjs_wifi_monitor_session_release(session); /* Last View, still one JS Session owner. */
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(&session->resources,&snapshot);
    assert(owners()==1 && atomic_load(&session->references)==1 && !snapshot.initialized && !snapshot.allocated_bytes);
    assert(!snapshot.counters.leased_frames && snapshot.counters.accepted==1);
    assert(esp32_mquickjs_wifi_monitor_diagnostics_snapshot(diagnostics,&unavailable)==1 && !unavailable);
    assert(!diagnostics[0].resources.allocated_bytes && !diagnostics[0].resources.counters.leased_frames);
    assert(atomic_load(&session->references)==1);
    esp32_mquickjs_wifi_monitor_session_release(session);assert(!owners());
    assert(!esp32_mquickjs_wifi_monitor_diagnostics_snapshot(diagnostics,&unavailable) && !unavailable);
}
static void closed_control_does_not_pin_idle_pool(void) {
    session_t *session=create(&runtime_a);
    esp32_mquickjs_event_queue_t *queue=session->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    esp32_mquickjs_wifi_monitor_snapshot_t snapshot;
    esp32_mquickjs_wifi_monitor_resources_snapshot(&session->resources,&snapshot);
    assert(!snapshot.initialized && !snapshot.allocated_bytes && owners()==1);
    assert(atomic_load(&session->references)==2); /* JS Session plus closed JS queue. */
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    dispose_saved_queue(queue);esp32_mquickjs_wifi_monitor_session_release(session);assert(!owners());
}
static void source_outlives_frame_session_and_js_wrapper(void) {
    session_t *session=create(&runtime_a);
    esp32_mquickjs_event_queue_t *queue=session->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);publish_one(session);
    esp32_mquickjs_wifi_monitor_event_t event=pending;has_event=false;
    assert(esp32_mquickjs_wifi_monitor_take_event(&session->resources,&event));
    monitor_reference_t *source=heap_caps_calloc(1,sizeof(*source),MALLOC_CAP_8BIT);
    assert(source && esp32_mquickjs_wifi_monitor_session_retain(session));source->session=session;
    assert(esp32_mquickjs_wifi_monitor_retain_frame(&session->resources,&event,&source->payload));
    assert(monitor_source_length(source)==24);
    esp32_mquickjs_byte_span_source_t iterator={0},reopened={0};JSValue error=JS_UNDEFINED;
    assert(monitor_source_open(&ctx,JS_UNDEFINED,source,&iterator,&error));
    assert(!monitor_source_open(&ctx,JS_UNDEFINED,source,&reopened,&error) && error==JS_EXCEPTION);
    assert(esp32_mquickjs_wifi_monitor_close_frame(&session->resources,&event));
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    dispose_saved_queue(queue);esp32_mquickjs_wifi_monitor_session_release(session);
    monitor_source_destroy(&ctx,source); /* Generic Source JS finalizer during native iteration. */
    assert(source->destroy_requested && owners()==1);
    esp32_mquickjs_byte_span_t span={0};
    assert(iterator.next(&ctx,iterator.opaque,&span) && span.length==24 && span.data[0]==8);
    assert(!iterator.next(&ctx,iterator.opaque,&span));
    iterator.close(&ctx,iterator.opaque);assert(!owners());

    session=create(&runtime_a);queue=session->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);publish_one(session);
    event=pending;has_event=false;assert(esp32_mquickjs_wifi_monitor_take_event(&session->resources,&event));
    source=heap_caps_calloc(1,sizeof(*source),MALLOC_CAP_8BIT);
    assert(source && esp32_mquickjs_wifi_monitor_session_retain(session));source->session=session;
    assert(esp32_mquickjs_wifi_monitor_retain_frame(&session->resources,&event,&source->payload));
    assert(esp32_mquickjs_wifi_monitor_close_frame(&session->resources,&event));
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    dispose_saved_queue(queue);esp32_mquickjs_wifi_monitor_session_release(session);
    monitor_source_destroy(&ctx,source);assert(!owners()); /* Never opened: release immediately. */
}
static void replacement_preserves_radio_and_retired_frame(void) {
    session_t *old=create(&runtime_a),*next=create(&runtime_a);
    esp32_mquickjs_event_queue_t *old_queue=old->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(old)==ESP_OK);publish_one(old);
    esp32_mquickjs_wifi_monitor_event_t event=pending;has_event=false;
    assert(esp32_mquickjs_wifi_monitor_take_event(&old->resources,&event));
    esp32_mquickjs_wifi_monitor_ref_t retained={0};
    assert(esp32_mquickjs_wifi_monitor_retain_frame(&old->resources,&event,&retained));
    assert(esp32_mquickjs_wifi_monitor_session_retain(old)); /* View independent control owner. */
    assert(esp32_mquickjs_wifi_monitor_close_frame(&old->resources,&event));
    unsigned acquired=radio_acquires,released=radio_releases;
    uint32_t identity=old->capture.radio.identity;
    assert(esp32_mquickjs_wifi_monitor_session_replace(old,next)==ESP_ERR_INVALID_STATE); /* Running. */
    assert(old->capture.radio.identity==identity && !next->capture.radio.acquired);
    hold_radio_rx=true;
    assert(esp32_mquickjs_wifi_monitor_session_stop(old)==ESP_ERR_TIMEOUT);
    assert(esp32_mquickjs_wifi_monitor_session_replace(old,next)==ESP_ERR_INVALID_STATE); /* Entered RX/reaper. */
    hold_radio_rx=false;
    assert(esp32_mquickjs_reaper_poll(&registry,4,NULL)==1);
    assert(!old->reaper_registered && old->capture.state==ESP32_MQUICKJS_WIFI_MONITOR_STOPPED);
    for(unsigned nth=1;nth<=4;nth++) {
        session_t *failed=NULL;sdk_allocation_calls=0;fail_sdk_allocation_at=nth;
        assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,&runtime_a,&options,3,48,true,4,frame,&failed)==JS_EXCEPTION);
        fail_sdk_allocation_at=0;
        assert(!failed && owners()==2 && old->capture.radio.identity==identity && !atomic_load(&old->close_requested));
        assert(radio_acquires==acquired && radio_releases==released);
    }
    current_task=2;
    assert(esp32_mquickjs_wifi_monitor_session_replace(old,next)==ESP_ERR_INVALID_STATE);
    current_task=1;
    assert(esp32_mquickjs_wifi_monitor_session_replace(old,old)==ESP_ERR_INVALID_STATE);
    session_t *foreign=create(&runtime_b);
    assert(esp32_mquickjs_wifi_monitor_session_replace(old,foreign)==ESP_ERR_INVALID_STATE);finish(foreign);
    assert(esp32_mquickjs_wifi_monitor_session_replace(old,next)==ESP_OK);
    assert(atomic_load(&old->closed) && !old->runtime && !old->capture.radio.acquired);
    assert(next->capture.radio.identity==identity && next->capture.radio.acquired && next->generation!=old->generation);
    assert(radio_acquires==acquired && radio_releases==released);
    dispose_saved_queue(old_queue);esp32_mquickjs_wifi_monitor_session_release(old);
    assert(owners()==2 && atomic_load(&old->references)==1);
    assert(esp32_mquickjs_wifi_monitor_session_start(next)==ESP_OK && radio_acquires==acquired);
    const uint8_t *bytes;size_t length;
    assert(esp32_mquickjs_wifi_monitor_ref_data(&retained,&bytes,&length) && length==24 && bytes[0]==8);
    assert(esp32_mquickjs_wifi_monitor_release_ref(&retained));
    esp32_mquickjs_wifi_monitor_session_release(old);assert(owners()==1);
    finish(next);assert(!owners() && radio_releases==released+1);
}
static void batch_roots_and_view_survive_session_close(void) {
    session_t *session=create(&runtime_a);esp32_mquickjs_event_queue_t *queue=session->bridge.queue;
    assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);
    monitor_batch_t *batch=heap_caps_calloc(1,sizeof(*batch)+2*sizeof(batch->events[0]),MALLOC_CAP_8BIT);
    assert(batch && esp32_mquickjs_wifi_monitor_session_retain(session));batch->session=session;batch->capacity=2;
    publish_one(session);esp32_mquickjs_wifi_monitor_event_t first=pending;has_event=false;
    assert(monitor_batch_adopt_event(batch,&first));
    assert(!monitor_batch_adopt_event(batch,&first) && batch->count==1); /* No duplicate PUBLIC root. */
    publish_one(session);esp32_mquickjs_wifi_monitor_event_t second=pending;has_event=false;
    esp32_mquickjs_wifi_monitor_event_t stale=second;stale.generation++;
    assert(!monitor_batch_adopt_event(batch,&stale) && batch->count==1);
    assert(monitor_batch_adopt_event(batch,&second) && batch->count==2);
    assert(!monitor_batch_adopt_event(batch,&second)); /* Capacity check before touching another root. */
    esp32_mquickjs_wifi_monitor_ref_t view={0};
    assert(esp32_mquickjs_wifi_monitor_retain_frame(&session->resources,&first,&view));
    assert(esp32_mquickjs_wifi_monitor_session_retain(session));
    assert(esp32_mquickjs_wifi_monitor_session_close(session)==ESP_OK);
    dispose_saved_queue(queue);esp32_mquickjs_wifi_monitor_session_release(session);
    esp32_mquickjs_wifi_monitor_info_t info;
    assert(esp32_mquickjs_wifi_monitor_frame_info(&session->resources,&second,&info) && info.captured_length==24);
    monitor_batch_release(batch); /* Both Batch roots close; retained first View survives. */
    const uint8_t *data;size_t length;
    assert(owners()==1 && atomic_load(&session->references)==1);
    assert(esp32_mquickjs_wifi_monitor_ref_data(&view,&data,&length) && length==24 && data[0]==8);
    assert(!esp32_mquickjs_wifi_monitor_frame_info(&session->resources,&second,&info));
    assert(esp32_mquickjs_wifi_monitor_release_ref(&view));
    esp32_mquickjs_wifi_monitor_session_release(session);assert(!owners());
}
static uint32_t wire_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void wire_source_retention_failures_and_padding(void) {
    for(unsigned mode=0;mode<4;mode++) {
        session_t *session=create(&runtime_a);
        assert(esp32_mquickjs_wifi_monitor_session_start(session)==ESP_OK);
        esp32_mquickjs_wifi_monitor_event_t events[2];
        for(unsigned i=0;i<2;i++) {
            uint8_t packet[23]={8};
            bool metadata_only=mode==2;
            esp32_mquickjs_wifi_rx_target_view_t view={
                .status=metadata_only?ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY:ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET,
                .bytes=metadata_only?NULL:packet,.readable_length=metadata_only?0:sizeof(packet),
                .metadata={.available=true,.type=metadata_only?ESP32_MQUICKJS_WIFI_PACKET_MISC:ESP32_MQUICKJS_WIFI_PACKET_DATA,
                    .driver_length=27,.primary=6}};
            assert(esp32_mquickjs_wifi_monitor_publish(&session->resources,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,
                UINT64_C(4294967396)+i)==ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED);
            events[i]=pending;has_event=false;
            assert(esp32_mquickjs_wifi_monitor_take_event(&session->resources,&events[i]));
        }
        unsigned baseline=queue_allocations-queue_frees;
        bool invalid=false;
        for(unsigned nth=1;nth<=3;nth++) {
            fail_sdk_allocation_at=sdk_allocation_calls+nth;
            assert(!monitor_wire_source_create(session,events,2,&invalid) && !invalid);
            fail_sdk_allocation_at=0;
            assert(queue_allocations-queue_frees==baseline);
            assert(atomic_load(&session->references)==3);
        }
        assert(!monitor_wire_source_create(session,events,0,&invalid) && invalid);
        assert(!monitor_wire_source_create(session,events,129,&invalid) && invalid);
        esp32_mquickjs_wifi_monitor_event_t broken[2]={events[0],events[1]};
        ++broken[1].identity;
        assert(!monitor_wire_source_create(session,broken,2,&invalid) && invalid);
        assert(queue_allocations-queue_frees==baseline && atomic_load(&session->references)==3);
        monitor_wire_source_t *source=monitor_wire_source_create(session,events,2,&invalid);
        assert(source && !invalid && source->retained==2);
        size_t expected=mode==2?592:640;
        assert(monitor_wire_source_length(source)==expected);
        assert(!memcmp(source->control,"E32QMON1",8) && wire_u32(source->control+24)==expected);
        assert(wire_u32(source->control+32)==80 && wire_u32(source->control+56)==336);
        for(unsigned i=0;i<2;i++) assert(esp32_mquickjs_wifi_monitor_close_frame(&session->resources,&events[i]));
        finish(session); /* Source alone now owns the closed Session and both slots. */
        assert(owners()==1);
        if(mode==1) { /* Never-opened Source cleanup. */
            monitor_wire_source_destroy(&ctx,source);assert(!owners());continue;
        }
        esp32_mquickjs_byte_span_source_t stream={0},second={0};JSValue error=JS_UNDEFINED;
        assert(monitor_wire_source_open(&ctx,JS_UNDEFINED,source,&stream,&error));
        assert(!monitor_wire_source_open(&ctx,JS_UNDEFINED,source,&second,&error) && error==JS_EXCEPTION);
        monitor_wire_source_destroy(&ctx,source); /* JS wrapper gone while native reader runs. */
        assert(source->destroy_requested && owners()==1);
        esp32_mquickjs_byte_span_t span;size_t received=0;unsigned spans=0;
        while(stream.next(&ctx,stream.opaque,&span)) {
            assert(span.length && span.owner==JS_UNDEFINED && !span.dma_capable);
            if(spans==0)assert(span.length==592 && !memcmp(span.data,"E32QMON1",8));
            else if(spans%2)assert(span.length==23 && span.data[0]==8);
            else assert(span.length==1 && span.data[0]==0);
            received+=span.length;++spans;
            if(mode==3)break; /* Cancel with the first control span outstanding. */
        }
        if(mode!=3)assert(received==expected && spans==(mode==2?1:5));
        stream.close(&ctx,stream.opaque);
        assert(!owners());
    }
}
static void generation_exhaustion(void) {
    s_monitor_next_generation=UINT32_MAX;
    session_t *session=create(&runtime_a);assert(session->generation==UINT32_MAX);finish(session);
    session=NULL;
    assert(esp32_mquickjs_wifi_monitor_session_new(&ctx,&runtime_a,&options,2,24,false,2,frame,&session)==JS_EXCEPTION && !session);
    assert(!owners() && s_monitor_next_generation==0);
}
int main(void) {
    esp32_mquickjs_reaper_init(&registry);
    failures();delayed_cleanup();retirement_and_capacity();teardown_isolation();future_and_view_retention();closed_control_does_not_pin_idle_pool();source_outlives_frame_session_and_js_wrapper();replacement_preserves_radio_and_retired_frame();batch_roots_and_view_survive_session_close();wire_source_retention_failures_and_padding();generation_exhaustion();
    assert(!context_refs && !closed_calls && !fail_context && !fail_frame);
    assert(!critical_depth && !roots && notifications && !owners() && !esp32_mquickjs_reaper_pending(&registry));
}
