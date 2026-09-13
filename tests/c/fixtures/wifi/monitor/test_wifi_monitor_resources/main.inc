
typedef esp32_mquickjs_wifi_monitor_resources_t resources_t;
typedef esp32_mquickjs_wifi_monitor_event_t event_t;
typedef esp32_mquickjs_wifi_monitor_ref_t ref_t;
typedef esp32_mquickjs_wifi_monitor_snapshot_t snapshot_t;
#define ACCEPT ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT
#define PUBLISH ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED
typedef struct { unsigned calls, fail_at, live; } allocation_t;
static void *allocate_zero(size_t count,size_t size,void *opaque) {
    allocation_t *state=opaque;assert(!critical_depth);
    if(++state->calls==state->fail_at)return NULL;
    void *p=calloc(count,size);assert(p);++state->live;return p;
}
static void *allocate(size_t size,void *opaque) { return allocate_zero(1,size,opaque); }
static void free_allocation(void *p,void *opaque) {
    allocation_t *state=opaque;assert(!critical_depth && p && state->live);--state->live;free(p);
}
static pthread_mutex_t pause_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_changed=PTHREAD_COND_INITIALIZER;
static bool paused,proceed;
typedef struct {
    resources_t *resources;
    event_t event;
    const esp32_mquickjs_wifi_rx_target_view_t *view;
    bool reject,consume,reenter,block;
    unsigned calls;
} queue_t;
static snapshot_t snapshot(resources_t *resources) {
    snapshot_t result;esp32_mquickjs_wifi_monitor_resources_snapshot(resources,&result);return result;
}
static bool publish_event(const event_t *event,void *opaque) {
    queue_t *queue=opaque;assert(!critical_depth);queue->event=*event;++queue->calls;
    assert(snapshot(queue->resources).counters.publishers>0);
    if(queue->consume) {
        assert(esp32_mquickjs_wifi_monitor_take_event(queue->resources,event));
        assert(esp32_mquickjs_wifi_monitor_close_frame(queue->resources,event));
        if(queue->reenter) {
            queue->reenter=false;
            assert(esp32_mquickjs_wifi_monitor_publish(queue->resources,queue->view,ACCEPT,44)==PUBLISH);
        }
    }
    if(queue->block) {
        /* Only the test scheduler blocks; the real EventQueue hook must not. */
        assert(!pthread_mutex_lock(&pause_lock));paused=true;assert(!pthread_cond_broadcast(&pause_changed));
        while(!proceed)assert(!pthread_cond_wait(&pause_changed,&pause_lock));
        assert(!pthread_mutex_unlock(&pause_lock));
    }
    return !queue->reject;
}
static bool initialize(resources_t *resources,allocation_t *alloc,queue_t *queue,
                       uint32_t generation,unsigned capacity,unsigned snap,bool complete) {
    queue->resources=resources;
    esp32_mquickjs_wifi_monitor_allocator_t allocator={allocate_zero,allocate,free_allocation,alloc};
    return esp32_mquickjs_wifi_monitor_resources_init(resources,generation,capacity,snap,complete,
                                                     &allocator,publish_event,queue);
}
static void close_resources(resources_t *resources,allocation_t *alloc) {
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(resources,false));
    assert(esp32_mquickjs_wifi_monitor_resources_deinit(resources) && !alloc->live);
    assert(!snapshot(resources).initialized && !snapshot(resources).allocated_bytes);
}
static void finish(resources_t *resources,allocation_t *alloc) {
    close_resources(resources,alloc);
    assert(!pthread_mutex_destroy(&resources->lock));
}
static void allocation_failures(void) {
    size_t bytes=99;
    assert(!esp32_mquickjs_wifi_monitor_resources_size(0,24,&bytes) && !bytes);
    assert(!esp32_mquickjs_wifi_monitor_resources_size(UINT32_MAX,UINT32_MAX,&bytes) && !bytes);
    assert(!esp32_mquickjs_wifi_monitor_resources_size(129,24,&bytes));
    assert(!esp32_mquickjs_wifi_monitor_resources_size(1,16385,&bytes));
    assert(!esp32_mquickjs_wifi_monitor_resources_size(1,0,&bytes));
    assert(!esp32_mquickjs_wifi_monitor_resources_size(1,24,NULL));
    assert(esp32_mquickjs_wifi_monitor_resources_size(128,16384,&bytes));
    assert(bytes==128*(sizeof(esp32_mquickjs_wifi_monitor_slot_t)+16384));
    for(unsigned nth=1;nth<=2;nth++) {
        resources_t resources={0};allocation_t alloc={.fail_at=nth};queue_t queue={0};
        assert(!initialize(&resources,&alloc,&queue,0,1,24,false) && !alloc.calls);
        assert(!initialize(&resources,&alloc,&queue,1,1,24,false));
        assert(alloc.calls==nth && !alloc.live && !snapshot(&resources).initialized);
        alloc.fail_at=0;assert(initialize(&resources,&alloc,&queue,1,1,24,false));
        unsigned calls=alloc.calls;
        assert(!initialize(&resources,&alloc,&queue,2,1,24,false) && alloc.calls==calls);
        finish(&resources,&alloc);
    }
}
static void ownership(esp32_mquickjs_wifi_rx_target_view_t *view) {
    resources_t resources={0};allocation_t alloc={0};queue_t queue={0};
    assert(initialize(&resources,&alloc,&queue,1,1,12,false));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,1)==ESP32_MQUICKJS_WIFI_MONITOR_CLOSING);
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,2)==PUBLISH);
    event_t old=queue.event;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,3)==ESP32_MQUICKJS_WIFI_MONITOR_POOL_FULL);
    assert(esp32_mquickjs_wifi_monitor_take_event(&resources,&old));
    assert(!esp32_mquickjs_wifi_monitor_take_event(&resources,&old));
    assert(!esp32_mquickjs_wifi_monitor_discard_event(&resources,&old));
    esp32_mquickjs_wifi_monitor_info_t info;
    assert(esp32_mquickjs_wifi_monitor_frame_info(&resources,&old,&info));
    assert(info.readable_length==24 && info.captured_length==12 && info.truncated && !info.metadata_only);
    assert(info.callback_time_us==2 && info.driver.rssi==-55 && info.header_type_matches);
    ref_t a={0},b={0},extra={0};
    assert(esp32_mquickjs_wifi_monitor_retain_frame(&resources,&old,&a));
    assert(!esp32_mquickjs_wifi_monitor_retain_frame(&resources,&old,&a));
    assert(esp32_mquickjs_wifi_monitor_retain_frame(&resources,&old,&b));
    uint8_t expected[12];memcpy(expected,view->bytes,12);
    memset((void *)view->bytes,0xcc,24); /* Native callback storage may be reused. */
    assert(esp32_mquickjs_wifi_monitor_close_frame(&resources,&old));
    assert(!esp32_mquickjs_wifi_monitor_close_frame(&resources,&old));
    assert(!esp32_mquickjs_wifi_monitor_retain_frame(&resources,&old,&extra));
    assert(!esp32_mquickjs_wifi_monitor_frame_info(&resources,&old,&info));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,false));
    assert(!esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(!initialize(&resources,&alloc,&queue,2,1,24,false));
    const uint8_t *bytes=NULL;size_t length=0;
    assert(esp32_mquickjs_wifi_monitor_ref_data(&a,&bytes,&length) && length==12 && !memcmp(bytes,expected,12));
    assert(esp32_mquickjs_wifi_monitor_release_ref(&a));
    assert(!esp32_mquickjs_wifi_monitor_release_ref(&a));
    assert(!esp32_mquickjs_wifi_monitor_ref_data(&a,&bytes,&length) && !bytes && !length);
    assert(snapshot(&resources).counters.leased_frames==1);
    assert(esp32_mquickjs_wifi_monitor_ref_data(&b,&bytes,&length) && !memcmp(bytes,expected,12));
    assert(esp32_mquickjs_wifi_monitor_release_ref(&b));
    assert(snapshot(&resources).free_slots==1 && !snapshot(&resources).counters.leased_frames);
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,4)==PUBLISH);
    assert(queue.event.index==old.index && queue.event.identity!=old.identity);
    assert(!esp32_mquickjs_wifi_monitor_discard_event(&resources,&old));
    assert(!esp32_mquickjs_wifi_monitor_close_frame(&resources,&old));
    assert(snapshot(&resources).counters.leased_frames==1);
    assert(esp32_mquickjs_wifi_monitor_discard_event(&resources,&queue.event));
    assert(!esp32_mquickjs_wifi_monitor_discard_event(&resources,&queue.event));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,false));
    assert(esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(!initialize(&resources,&alloc,&queue,1,1,24,false));
    assert(initialize(&resources,&alloc,&queue,2,1,24,false));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,5)==PUBLISH);
    assert(queue.event.identity==old.identity && queue.event.generation!=old.generation);
    assert(!esp32_mquickjs_wifi_monitor_discard_event(&resources,&old));
    assert(esp32_mquickjs_wifi_monitor_discard_event(&resources,&queue.event));
    finish(&resources,&alloc);
}
static void drops_and_exhaustion(const esp32_mquickjs_wifi_rx_target_view_t *view) {
    resources_t resources={0};allocation_t alloc={0};queue_t queue={.reject=true};
    assert(initialize(&resources,&alloc,&queue,1,1,24,false));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,1)==ESP32_MQUICKJS_WIFI_MONITOR_QUEUE_FULL);
    assert(snapshot(&resources).free_slots==1 && !snapshot(&resources).counters.publishers);
    assert(!esp32_mquickjs_wifi_monitor_take_event(&resources,&queue.event));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,NULL,ACCEPT,2)==ESP32_MQUICKJS_WIFI_MONITOR_INVALID);
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,(esp32_mquickjs_wifi_rx_filter_result_t)-1,2)==ESP32_MQUICKJS_WIFI_MONITOR_INVALID);
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,NULL,ESP32_MQUICKJS_WIFI_RX_FILTER_RATE,2)==ESP32_MQUICKJS_WIFI_MONITOR_FILTERED);
    queue.reject=false;queue.consume=true;queue.reenter=true;queue.view=view;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,3)==PUBLISH);
    snapshot_t state=snapshot(&resources);
    assert(state.counters.accepted==2 && state.counters.captured_bytes==48 && state.free_slots==1);
    assert(!state.counters.publishers && !state.counters.leased_frames && state.counters.dropped_queue_full==1);
    assert(state.counters.invalid_callback_data==2 && state.counters.filtered[ESP32_MQUICKJS_WIFI_RX_FILTER_RATE]==1);
    /* Inject the production counters at their final values; no wrap/reuse. */
    resources.counters.callbacks=resources.counters.accepted=resources.counters.captured_bytes=UINT64_MAX;
    resources.next_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,4)==PUBLISH);
    assert(queue.event.identity==UINT32_MAX && snapshot(&resources).identity_exhausted);
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,5)==ESP32_MQUICKJS_WIFI_MONITOR_IDENTITY_EXHAUSTED);
    assert(!esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    state=snapshot(&resources);
    assert(state.counters.callbacks==UINT64_MAX && state.counters.accepted==UINT64_MAX && state.counters.captured_bytes==UINT64_MAX);
    finish(&resources,&alloc);
}
static void complete_and_metadata(const esp32_mquickjs_wifi_rx_target_view_t *view) {
    resources_t resources={0};allocation_t alloc={0};queue_t queue={0};
    assert(initialize(&resources,&alloc,&queue,1,1,12,true));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ACCEPT,1)==ESP32_MQUICKJS_WIFI_MONITOR_REQUIRED_COMPLETE);
    esp32_mquickjs_wifi_rx_target_view_t meta=*view;
    meta.status=ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY;meta.bytes=NULL;meta.readable_length=0;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,&meta,ACCEPT,2)==ESP32_MQUICKJS_WIFI_MONITOR_REQUIRED_COMPLETE);
    assert(snapshot(&resources).counters.dropped_required_complete==2 && !queue.calls);
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,false));
    assert(esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(initialize(&resources,&alloc,&queue,2,1,24,false));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,&meta,ACCEPT,3)==PUBLISH);
    assert(esp32_mquickjs_wifi_monitor_take_event(&resources,&queue.event));
    esp32_mquickjs_wifi_monitor_info_t info;
    assert(esp32_mquickjs_wifi_monitor_frame_info(&resources,&queue.event,&info));
    assert(info.metadata_only && !info.truncated && !info.captured_length);
    assert(esp32_mquickjs_wifi_monitor_close_frame(&resources,&queue.event));
    meta.bytes=view->bytes;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,&meta,ACCEPT,4)==ESP32_MQUICKJS_WIFI_MONITOR_INVALID);
    close_resources(&resources,&alloc);
    esp32_mquickjs_wifi_rx_target_view_t partial=*view;
    partial.metadata.driver_length=(uint16_t)(view->readable_length+4);
    assert(initialize(&resources,&alloc,&queue,3,1,32,true));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,&partial,ACCEPT,5)==ESP32_MQUICKJS_WIFI_MONITOR_REQUIRED_COMPLETE);
    close_resources(&resources,&alloc);
    assert(initialize(&resources,&alloc,&queue,4,1,32,false));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,&partial,ACCEPT,6)==PUBLISH);
    assert(esp32_mquickjs_wifi_monitor_take_event(&resources,&queue.event));
    assert(esp32_mquickjs_wifi_monitor_frame_info(&resources,&queue.event,&info));
    assert(info.truncated && info.captured_length==info.readable_length && info.readable_length<info.driver.driver_length);
    assert(esp32_mquickjs_wifi_monitor_close_frame(&resources,&queue.event));
    finish(&resources,&alloc);
}
static void *publisher(void *opaque) {
    queue_t *queue=opaque;
    assert(esp32_mquickjs_wifi_monitor_publish(queue->resources,queue->view,ACCEPT,1)==
           (queue->reject?ESP32_MQUICKJS_WIFI_MONITOR_QUEUE_FULL:PUBLISH));
    return NULL;
}
static void close_during_publication(const esp32_mquickjs_wifi_rx_target_view_t *view,bool consume) {
    resources_t resources={0};allocation_t alloc={0};
    queue_t queue={.view=view,.block=true,.consume=consume,.reject=!consume};
    paused=false;proceed=false;
    assert(initialize(&resources,&alloc,&queue,1,1,24,false));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    pthread_t worker;assert(!pthread_create(&worker,NULL,publisher,&queue));
    assert(!pthread_mutex_lock(&pause_lock));
    while(!paused)assert(!pthread_cond_wait(&pause_changed,&pause_lock));
    assert(!pthread_mutex_unlock(&pause_lock));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,false));
    snapshot_t state=snapshot(&resources);
    assert(state.counters.publishers==1 && state.counters.leased_frames==(consume?0U:1U));
    uint32_t identity=resources.next_identity;
    esp32_mquickjs_wifi_monitor_resources_reset_counters(&resources);
    snapshot_t reset_state=snapshot(&resources);
    assert(reset_state.counters.callbacks==0 && reset_state.counters.accepted==0);
    assert(reset_state.counters.publishers==state.counters.publishers && reset_state.counters.leased_frames==state.counters.leased_frames);
    assert(resources.next_identity==identity && reset_state.generation==state.generation && alloc.live==2);
    /* Even a synchronously consumed root cannot free a still-running hook. */
    assert(!esp32_mquickjs_wifi_monitor_resources_deinit(&resources) && alloc.live==2);
    assert(!initialize(&resources,&alloc,&queue,2,1,24,false));
    assert(!pthread_mutex_lock(&pause_lock));proceed=true;assert(!pthread_cond_broadcast(&pause_changed));
    assert(!pthread_mutex_unlock(&pause_lock));assert(!pthread_join(worker,NULL));
    assert(!snapshot(&resources).counters.publishers && !snapshot(&resources).counters.leased_frames);
    finish(&resources,&alloc);
}
int main(void) {
    wifi_pkt_rx_ctrl_t rx={0};rx.sig_len=24;rx.rssi=-55;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=24;
#endif
    uint8_t packet[sizeof(rx)+24];memcpy(packet,&rx,sizeof(rx));memset(packet+sizeof(rx),0,24);
    packet[sizeof(rx)]=8;
    esp32_mquickjs_wifi_rx_target_view_t view;
    assert(esp32_mquickjs_wifi_rx_target_view(packet,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    allocation_failures();ownership(&view);drops_and_exhaustion(&view);complete_and_metadata(&view);
    close_during_publication(&view,false);close_during_publication(&view,true);
    assert(!critical_depth);
}
