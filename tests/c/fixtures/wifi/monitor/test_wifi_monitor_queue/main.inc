
static JSContext ctx;
static esp32_mquickjs_runtime_t runtime;
static esp32_mquickjs_wifi_monitor_resources_t resources;
static esp32_mquickjs_wifi_monitor_queue_t bridge;
static unsigned pool_allocations;
static void *pool_calloc(size_t count,size_t size,void *opaque) { (void)opaque;++pool_allocations;return calloc(count,size); }
static void *pool_malloc(size_t size,void *opaque) { return pool_calloc(1,size,opaque); }
static void pool_free(void *p,void *opaque) { (void)opaque;assert(pool_allocations);--pool_allocations;free(p); }
static bool retain_context(void *opaque) { assert(opaque==&bridge);if(fail_context)return false;++context_refs;return true; }
static void release_context(void *opaque) { assert(opaque==&bridge && context_refs && !critical_depth);--context_refs; }
static void request_close(void *opaque) { assert(opaque==&bridge);++closed_calls; }
static JSValue make_frame(JSContext *context,const esp32_mquickjs_wifi_monitor_event_t *event,void *opaque) {
    (void)context;assert(opaque==&bridge);
    esp32_mquickjs_wifi_monitor_info_t info;
    assert(esp32_mquickjs_wifi_monitor_frame_info(&resources,event,&info));
    if(fail_frame)return JS_EXCEPTION;
    assert(retain_context(opaque)); /* Frame holds context independently of queue. */
    return 99;
}
static void setup(void) {
    assert(!pool_allocations && !context_refs && !roots && !has_event);
    memset(&resources,0,sizeof(resources));memset(&bridge,0,sizeof(bridge));
    bridge.resources=&resources;bridge.make_frame=make_frame;bridge.retain_context=retain_context;
    bridge.release_context=release_context;bridge.request_close=request_close;bridge.opaque=&bridge;
    esp32_mquickjs_wifi_monitor_allocator_t allocator={pool_calloc,pool_malloc,pool_free,NULL};
    assert(esp32_mquickjs_wifi_monitor_resources_init(&resources,1,1,24,false,&allocator,
           esp32_mquickjs_wifi_monitor_queue_publish,&bridge));
}
static void cleanup(void) {
    assert(!context_refs && !roots && !has_event && !bridge.queue && !atomic_load(&bridge.context_owned));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,false));
    assert(esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    assert(!pthread_mutex_destroy(&resources.lock));
}
static void constructor_failures(void) {
    bool *flags[]={&fail_context,&fail_allocation,&fail_resources,&fail_object,&fail_lookup,&fail_bind};
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        setup();*flags[i]=true;
        assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==JS_EXCEPTION);
        *flags[i]=false;cleanup();
    }
    setup();
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,129)==JS_EXCEPTION);
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==JS_EXCEPTION);
    cleanup();
}
static void late_future(const esp32_mquickjs_wifi_rx_target_view_t *view,bool finish,bool failure) {
    setup();fail_frame=failure;
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==17 && context_refs==1);
    esp32_mquickjs_event_queue_t *queue=bridge.queue;
    assert(!esp32_mquickjs_event_queue_bind_context_release(queue,monitor_queue_release_context));
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    queue_full=true;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,1)==ESP32_MQUICKJS_WIFI_MONITOR_QUEUE_FULL);
    queue_full=false;
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,2)==ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED);
    esp32_mquickjs_future_driver_state_t *future=calloc(1,sizeof(*future));assert(future);
    future->event=malloc(sizeof(pending));assert(future->event);memcpy(future->event,&pending,sizeof(pending));has_event=false;
    esp32_mquickjs_wifi_monitor_event_t delivered=pending;
    future->queue=queue;future->received=true;future->native_queue_retained=true;
    assert(esp32_mquickjs_event_queue_retain(queue));
    esp32_mquickjs_wifi_monitor_queue_detach(&bridge);
    assert(!bridge.queue && atomic_load(&bridge.context_owned) && context_refs==1);
    unsigned before_free=queue_frees;
    assert(esp32_mquickjs_event_queue_dispose(&ctx,17) && !js_opaque && context_refs==1 && queue_frees==before_free);
    assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==JS_EXCEPTION);
    assert(!esp32_mquickjs_wifi_monitor_resources_deinit(&resources));
    if(finish)assert(event_queue_future_finish(&ctx,future)==(failure?JS_EXCEPTION:99));
    event_queue_future_destroy(future);
    assert(context_refs==(finish && !failure?1U:0U) && !atomic_load(&bridge.context_owned));
    if(finish && !failure) {
        assert(esp32_mquickjs_wifi_monitor_close_frame(&resources,&delivered));
        release_context(&bridge);
    }
    assert(!esp32_mquickjs_wifi_monitor_discard_event(&resources,&delivered));
    cleanup();fail_frame=false;
}
static void runtime_close(const esp32_mquickjs_wifi_rx_target_view_t *view) {
    setup();assert(esp32_mquickjs_wifi_monitor_queue_new(&ctx,&runtime,&bridge,1)==17);
    assert(esp32_mquickjs_wifi_monitor_resources_set_accepting(&resources,true));
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,3)==ESP32_MQUICKJS_WIFI_MONITOR_ACCEPTED);
    unsigned before=closed_calls;
    assert(esp32_mquickjs_event_queue_close(bridge.queue) && closed_calls==before+1);
    assert(esp32_mquickjs_wifi_monitor_publish(&resources,view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,4)==ESP32_MQUICKJS_WIFI_MONITOR_CLOSING);
    assert(!esp32_mquickjs_event_queue_close(bridge.queue) && closed_calls==before+1);
    assert(esp32_mquickjs_event_queue_dispose(&ctx,17) && context_refs==1 && !has_event);
    esp32_mquickjs_wifi_monitor_queue_detach(&bridge);
    esp32_mquickjs_wifi_monitor_queue_detach(&bridge);
    cleanup();
}
int main(void) {
    wifi_pkt_rx_ctrl_t rx={0};rx.sig_len=24;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=24;
#endif
    uint8_t packet[sizeof(rx)+24];memcpy(packet,&rx,sizeof(rx));memset(packet+sizeof(rx),0,24);packet[sizeof(rx)]=8;
    esp32_mquickjs_wifi_rx_target_view_t view;
    assert(esp32_mquickjs_wifi_rx_target_view(packet,WIFI_PKT_DATA,&view)==ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET);
    constructor_failures();late_future(&view,false,false);late_future(&view,true,true);late_future(&view,true,false);runtime_close(&view);
    assert(!critical_depth && !roots && !pool_allocations && !context_refs && queue_allocations && resource_frees);
}
