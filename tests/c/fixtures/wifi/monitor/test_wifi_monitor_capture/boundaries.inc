
static esp32_mquickjs_wifi_monitor_capture_t capture;
static esp32_mquickjs_wifi_monitor_resources_t resources;
static esp32_mquickjs_event_queue_t queue;
static esp32_mquickjs_wifi_monitor_queue_t bridge;
static esp32_mquickjs_wifi_rx_target_view_t view;
static unsigned fail_stage,acquires,subscribes,channel_releases,radio_releases,detaches,discard_calls,pool_allocations;
static bool hold_rx,hold_radio,subscribe_retained,channel_conflict,ps_none=true,cancel_start;
static int cleanup_error;
static void (*active_sink)(void *,const esp32_mquickjs_wifi_rx_target_view_t *,esp32_mquickjs_wifi_rx_filter_result_t,uint64_t);
static void *sink_context;
static bool esp32_mquickjs_event_queue_is_closed(const esp32_mquickjs_event_queue_t *q) { return q->closed; }
static size_t esp32_mquickjs_event_queue_discard_all(esp32_mquickjs_event_queue_t *q) {
    assert(!capture.promiscuous.acquired);++discard_calls;
    if(!q->has_event)return 0;
    q->has_event=false;assert(esp32_mquickjs_wifi_monitor_discard_event(&resources,&q->event));return 1;
}
static void esp32_mquickjs_wifi_monitor_queue_detach(esp32_mquickjs_wifi_monitor_queue_t *b) {
    assert(!capture.radio.acquired && !capture.promiscuous.acquired && !capture.channel_claimed);
    ++detaches;b->queue=NULL;queue.closed=true;
}
static int esp32_mquickjs_wifi_radio_acquire(int client,int mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && client==ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR && mode==WIFI_MODE_STA);++acquires;
    if(fail_stage==1)return -11;
    *lease=(esp32_mquickjs_wifi_radio_lease_t){.generation=7,.identity=9,.client=client,.acquired=true};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_ensure_started(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && lease->acquired);return fail_stage==2?-12:ESP_OK;
}
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *status) {
    assert(!critical_depth);if(fail_stage==3)return -13;
    *status=(esp32_mquickjs_wifi_radio_status_t){.generation=7,.power_save_available=true,.power_save=ps_none?0:1,
        .cleanup_error=cleanup_error,.cleanup_stage=cleanup_error?"promiscuous-restore-enable":NULL};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_set_channel(esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t primary,int secondary) {
    assert(!critical_depth && lease->acquired && primary==6 && secondary==0);return fail_stage==4?-14:ESP_OK;
}
static int esp32_mquickjs_wifi_radio_get_channel(uint8_t *primary,int *secondary,uint32_t *generation) {
    assert(!critical_depth);if(fail_stage==5)return -15;*primary=6;*secondary=0;*generation=8;return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_lease_channel_status(const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_radio_channel_status_t *status) {
    assert(lease->acquired);
    *status=(esp32_mquickjs_wifi_radio_channel_status_t){.primary=6,.fixed=true,.conflicted=channel_conflict};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_subscribe_promiscuous(esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,const esp32_mquickjs_wifi_rx_filter_t *filter,
    void (*sink)(void *,const esp32_mquickjs_wifi_rx_target_view_t *,esp32_mquickjs_wifi_rx_filter_result_t,uint64_t),void *context,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *token) {
    assert(!critical_depth && lease->acquired && subscriber && esp32_mquickjs_wifi_rx_filter_valid(filter));++subscribes;
    if(fail_stage==6 && !subscribe_retained)return -16;
    *token=(esp32_mquickjs_wifi_radio_promiscuous_lease_t){.generation=7,.identity=subscribes,.radio_lease_identity=9,.acquired=true};
    active_sink=sink;sink_context=context;
    if(fail_stage==6)return -16;
    if(cancel_start)esp32_mquickjs_wifi_monitor_capture_request_stop(context,true);
    sink(context,&view,ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT,10);
    return ESP_OK;
}
static void esp32_mquickjs_wifi_radio_release_promiscuous(esp32_mquickjs_wifi_radio_promiscuous_lease_t *token) {
    assert(!critical_depth && token->acquired);
    if(hold_rx)return;
    memset(token,0,sizeof(*token));active_sink=NULL;sink_context=NULL;
}
static void esp32_mquickjs_wifi_radio_release_channel(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && lease->acquired && !capture.promiscuous.acquired);++channel_releases;
}
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && lease->acquired && !capture.promiscuous.acquired && !capture.channel_claimed);
    ++radio_releases;if(!hold_radio)memset(lease,0,sizeof(*lease));
}
static bool publish(const esp32_mquickjs_wifi_monitor_event_t *event,void *opaque) {
    (void)opaque;assert(!critical_depth);
    if(queue.closed || queue.has_event)return false;
    queue.event=*event;queue.has_event=true;return true;
}
static void *allocate_zero(size_t count,size_t size,void *opaque) { (void)opaque;++pool_allocations;return calloc(count,size); }
static void *allocate(size_t size,void *opaque) { return allocate_zero(1,size,opaque); }
static void free_pool(void *p,void *opaque) { (void)opaque;assert(pool_allocations);--pool_allocations;free(p); }
