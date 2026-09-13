
static bool resume_allowed=true;
bool esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(const esp32_mquickjs_wifi_twt_information_identity_t *native){
    assert(!locked && native->node==17);return resume_allowed;
}

static void esp32_mquickjs_wifi_twt_tx_cleanup_native(void){assert(!locked);}
static esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t id){
    assert(!locked);(void)id;return tx_error;
}
void esp32_mquickjs_wifi_twt_information_timer_snapshot(esp32_mquickjs_wifi_twt_information_timer_snapshot_t *out){
    assert(!locked);*out=(esp32_mquickjs_wifi_twt_information_timer_snapshot_t){.fault=timer_error};
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks){
    assert(!locked && base==WIFI_EVENT && id==31 && size==40 && !ticks);++posts;
    const wifi_event_sta_itwt_suspend_t *event=data;const uint8_t *bytes=data;
    assert(!bytes[5] && !bytes[6] && !bytes[7]);
    for(unsigned i=0;i<8;++i)if(!(event->flow_id_bitmap&(1U<<i)))assert(!event->actual_suspend_time_ms[i]);
    if(inspect_post){
        esp32_mquickjs_wifi_twt_information_result_t result;
        assert(esp32_mquickjs_wifi_twt_information_read(current_identity,&result) && result.complete);
        esp32_mquickjs_wifi_twt_information_abandon(current_identity);
        assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==ESP_ERR_NOT_FINISHED);
    }
    return post_error;
}
