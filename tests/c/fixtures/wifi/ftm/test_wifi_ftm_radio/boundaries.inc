
#define WIFI_EVENT_FTM_REPORT 17
#define ESP_ERR_INVALID_RESPONSE -3
static unsigned ftm_start_calls,ftm_end_calls,report_calls,timer_creates,timer_starts,ftm_posts;
static int ftm_start_error,ftm_end_error,report_error,timer_error,ftm_post_error;
static bool ftm_early,report_race,ftm_queued,timer_pending;
static wifi_radio_ftm_fence_t ftm_marker;
static esp_timer_create_args_t timer_args;
static void wifi_radio_ftm_event(int32_t,const void *);
static void ftm_event(void) {
    wifi_event_ftm_report_t event={.status=FTM_STATUS_SUCCESS,.rtt_raw=10,.rtt_est=12,.dist_est=30,.ftm_report_num_entries=70};
    memcpy(event.peer_mac,s_ftm.config.resp_mac,6);wifi_radio_ftm_event(WIFI_EVENT_FTM_REPORT,&event);
}
#undef esp_wifi_ftm_initiate_session
#undef esp_wifi_ftm_end_session
static int esp_wifi_ftm_initiate_session(wifi_ftm_initiator_cfg_t *config) {
    assert(locks && !critical && config==&s_ftm.config && s_ftm.lease.acquired && s_ftm.state.dispatching);
    ++ftm_start_calls;if(ftm_early)ftm_event();return ftm_start_error;
}
static int esp_wifi_ftm_end_session(void){assert(locks && !critical);++ftm_end_calls;return ftm_end_error;}
#define esp_wifi_ftm_initiate_session(...) WIFI_RADIO_MUTATION(esp_wifi_ftm_initiate_session(__VA_ARGS__))
#define esp_wifi_ftm_end_session(...) WIFI_RADIO_MUTATION(esp_wifi_ftm_end_session(__VA_ARGS__))
static int esp_wifi_ftm_get_report(wifi_ftm_report_entry_t *entries,uint8_t count) {
    assert(locks && !critical && s_ftm.state.sdk_fenced && count<=64);++report_calls;
    if(report_error)return report_error;
    if(entries)for(unsigned i=0;i<count;i++)entries[i]=(wifi_ftm_report_entry_t){.rtt=i,.t1=UINT64_MAX-i,.rssi=-30};
    else assert(!count);
    if(report_race)ftm_event();return 0;
}
static int esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(locks && !critical && !*out);++timer_creates;if(timer_error)return timer_error;
    timer_args=*args;*out=(void *)1;return 0;
}
static int esp_timer_start_once(esp_timer_handle_t handle,uint64_t delay) {
    assert(locks && !critical && handle && delay && !timer_pending);++timer_starts;
    if(timer_error)return timer_error;timer_pending=true;return 0;
}
static int ftm_event_post(esp_event_base_t base,int32_t id,const void *data,size_t bytes,unsigned ticks) {
    assert(locks && !critical && base==7 && id==3 && bytes==sizeof(ftm_marker) && !ticks);++ftm_posts;
    if(ftm_post_error)return ftm_post_error;ftm_marker=*(const wifi_radio_ftm_fence_t *)data;ftm_queued=true;return 0;
}
#define esp_event_post ftm_event_post
