
static int sleep_type=1,information_replace_error,information_tx_error,information_send_error;
static unsigned information_begins,information_submits,information_resumes;
static bool information_resume;
int pm_get_sleep_type(void){assert(ioctl_task);return sleep_type;}
esp_err_t esp32_mquickjs_wifi_twt_information_begin_native(uint32_t id,
    const esp32_mquickjs_wifi_twt_information_identity_t *native,uint32_t ms,bool resume,uint32_t *out){
    assert(ioctl_task && !locked && id==cancel_result.identity && native->node && native->flows==8);
    assert(native->request_ids[3]==123 && ms==(resume?0U:120U));
    information_resume=resume;*out=77;++information_begins;return 0;
}
void esp32_mquickjs_wifi_twt_information_submitted_native(uint32_t id,esp_err_t error){
    assert(ioctl_task && id==77 && error==information_send_error);++information_submits;
}
esp_err_t esp32_mquickjs_wifi_twt_information_reap_native(uint32_t id){assert(ioctl_task);(void)id;return 0;}
esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t id){assert(ioctl_task && !id);return information_tx_error;}
esp_err_t esp32_mquickjs_wifi_twt_information_timer_replaceable_native(int16_t id,unsigned flow){
    assert(ioctl_task && id==123 && flow==3);return information_replace_error;
}
int wifi_sta_itwt_suspend_process(void *message){
    assert(ioctl_task && !information_resume && ((uint8_t *)message)[8]==3 && ((uint8_t *)message)[9]==3);
    uint32_t ms;memcpy(&ms,(uint8_t *)message+12,4);assert(ms==120);return information_send_error;
}
int ieee80211_itwt_information(void *node,uint32_t flow,uint32_t size,uint32_t all,uint32_t ms){
    assert(ioctl_task && information_resume && node && flow==3 && size==3 && !all && !ms);
    ++information_resumes;return information_send_error;
}
esp_err_t esp32_mquickjs_wifi_twt_information_post(const void *data,size_t size){(void)data;(void)size;assert(0);return ESP_ERR_INVALID_STATE;}
