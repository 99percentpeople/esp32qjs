
static unsigned cancels,queries,releases,event_posts;
static int cancel_error,query_error,release_error,event_error;
static esp32_mquickjs_wifi_twt_setup_cut_t current_cut={.tx_revision=17,.timer_revision=3};
static unsigned tx_releases;
static int tx_release_error;
static esp32_mquickjs_wifi_twt_setup_event_fence_t queued;
static bool event_queued;
static esp32_mquickjs_wifi_twt_setup_result_t result(uint32_t id) {
    esp32_mquickjs_wifi_twt_setup_result_t out;
    assert(esp32_mquickjs_wifi_twt_setup_result_read(id,&out)==0);return out;
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
    assert(!locked && ticks==0);
    if(base==WIFI_EVENT){assert((id==28 && size==32) || (id==29 && size==8));return 0;}
    assert(base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==5 && size==8);
    assert(!timer_native.live && !timer_native.running && native_calls);++event_posts;
    if(event_error)return event_error;
    queued=*(const esp32_mquickjs_wifi_twt_setup_event_fence_t *)data;event_queued=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_cancel(uint32_t id) {
    assert(!locked);++cancels;
    if(cancel_error)return cancel_error;
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(id,8));return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_quiescent(uint32_t id,esp32_mquickjs_wifi_twt_setup_cut_t *out) {
    assert(!locked && (result(id).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED));++queries;
    if(query_error)return query_error;*out=current_cut;return 0;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_release(uint32_t id,const esp32_mquickjs_wifi_twt_setup_cut_t *cut,uint32_t sequence) {
    assert(!locked);++releases;
    if(release_error)return release_error;
    if(memcmp(cut,&current_cut,sizeof(*cut)))return ESP_ERR_NOT_FINISHED;
    esp32_mquickjs_wifi_twt_setup_result_t got=result(id);
    return esp32_mquickjs_wifi_twt_setup_result_release_native(id,got.revision,sequence)?0:ESP_ERR_NOT_FINISHED;
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_teardown_tx_release(uint32_t id,uint32_t revision) {
    esp32_mquickjs_wifi_twt_setup_result_t gone;
    assert(!locked && revision==current_cut.teardown_revision);
    assert(esp32_mquickjs_wifi_twt_setup_result_read(id,&gone)==ESP_ERR_INVALID_STATE);
    ++tx_releases;return tx_release_error;
}
