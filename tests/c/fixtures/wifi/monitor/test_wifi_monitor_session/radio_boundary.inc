
static bool hold_radio_rx;
static unsigned radio_acquires,radio_releases;
bool esp32_mquickjs_event_queue_is_closed(const esp32_mquickjs_event_queue_t *q) { return atomic_load(&q->closed); }
static int esp32_mquickjs_wifi_radio_acquire(int client,int mode,esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(!critical_depth && mode==WIFI_MODE_STA);++radio_acquires;
    *lease=(esp32_mquickjs_wifi_radio_lease_t){.generation=1,.identity=radio_acquires,.client=client,.acquired=true};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_ensure_started(esp32_mquickjs_wifi_radio_lease_t *lease) { assert(lease->acquired);return ESP_OK; }
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *status) {
    *status=(esp32_mquickjs_wifi_radio_status_t){.generation=1,.power_save_available=true};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_get_channel(uint8_t *p,int *s,uint32_t *g) { *p=6;*s=0;*g=1;return ESP_OK; }
static int esp32_mquickjs_wifi_radio_set_channel(esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t p,int s) {
    assert(lease->acquired && p==6 && s==0);return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_lease_channel_status(const esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_radio_channel_status_t *status) {
    assert(lease->acquired);*status=(esp32_mquickjs_wifi_radio_channel_status_t){.primary=6,.fixed=true};return ESP_OK;
}
static int esp32_mquickjs_wifi_radio_subscribe_promiscuous(esp32_mquickjs_wifi_radio_lease_t *lease,
    esp32_mquickjs_wifi_promiscuous_subscriber_t *subscriber,const esp32_mquickjs_wifi_rx_filter_t *filter,
    void (*sink)(void *,const esp32_mquickjs_wifi_rx_target_view_t *,esp32_mquickjs_wifi_rx_filter_result_t,uint64_t),void *context,
    esp32_mquickjs_wifi_radio_promiscuous_lease_t *token) {
    assert(lease->acquired && subscriber && esp32_mquickjs_wifi_rx_filter_valid(filter) && sink && context);
    *token=(esp32_mquickjs_wifi_radio_promiscuous_lease_t){.identity=lease->identity,.acquired=true};return ESP_OK;
}
static void esp32_mquickjs_wifi_radio_release_promiscuous(esp32_mquickjs_wifi_radio_promiscuous_lease_t *token) {
    assert(!critical_depth);if(!hold_radio_rx)memset(token,0,sizeof(*token));
}
static void esp32_mquickjs_wifi_radio_release_channel(esp32_mquickjs_wifi_radio_lease_t *lease) { assert(lease->acquired); }
static void esp32_mquickjs_wifi_radio_release(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(lease->acquired);++radio_releases;memset(lease,0,sizeof(*lease));
}
