
static bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime) {
    (void)runtime;assert(!critical);
    if(cancelled)return false;
    if(script_at<script_count)wifi_radio_observe_lifecycle_event(script[script_at++]);
    else if(late_start){late_start=false;wifi_radio_observe_lifecycle_event(WIFI_EVENT_STA_START);}
    return true;
}
static int esp_event_post(esp_event_base_t base,int id,const void *data,size_t size,TickType_t wait) {
    assert(!critical && size==sizeof(wifi_radio_event_fence_t) && !wait);++posts;
    if(post_error)return post_error;
    wifi_radio_event_fence_t marker=*(const wifi_radio_event_fence_t *)data;
    if(late_stop && posts==1) {
        wifi_radio_observe_lifecycle_event(WIFI_EVENT_STA_STOP);
        late_start=true;
    }
    wifi_radio_lifecycle_fence(NULL,base,id,&marker);
    return ESP_OK;
}
static void reset(wifi_mode_t mode) {
    memset(&s_radio,0,sizeof(s_radio));s_radio.event_live=mode;
    critical=posts=ticks=script_at=script_count=0;post_error=0;cancelled=late_stop=late_start=false;
    assert(wifi_radio_begin_events(RADIO_EVENTS_RESTART,mode)==ESP_OK);
}
static void append(int event) {assert(script_count<24);script[script_count++]=event;}
int main(void) {
    reset(WIFI_MODE_STA);
    append(WIFI_EVENT_STA_START); /* A cached/lone START is not a completed cycle. */
    assert(wifi_radio_wait_events_inner(NULL)==ESP_ERR_TIMEOUT && !posts);
    assert(!(s_radio.event_seen & WIFI_MODE_STA));
    reset(WIFI_MODE_STA);append(WIFI_EVENT_STA_STOP);
    assert(wifi_radio_wait_events_inner(NULL)==ESP_ERR_TIMEOUT && !posts && !s_radio.event_live);
    reset(WIFI_MODE_STA);append(WIFI_EVENT_STA_START);append(WIFI_EVENT_STA_STOP);append(WIFI_EVENT_STA_START);
    assert(wifi_radio_wait_events_inner(NULL)==ESP_OK && posts==1 && script_at==3 && s_radio.event_phase==RADIO_EVENTS_IDLE);
    reset(WIFI_MODE_APSTA);append(WIFI_EVENT_STA_START);append(WIFI_EVENT_AP_STOP);append(WIFI_EVENT_AP_START);
    append(WIFI_EVENT_STA_STOP);append(WIFI_EVENT_STA_START);
    assert(wifi_radio_wait_events_inner(NULL)==ESP_OK && posts==1 && script_at==5 && s_radio.event_live==WIFI_MODE_APSTA);
    reset(WIFI_MODE_STA);append(WIFI_EVENT_STA_STOP);append(WIFI_EVENT_STA_START);late_stop=true;
    assert(wifi_radio_wait_events_inner(NULL)==ESP_OK && posts==2 && !late_start);
    reset(WIFI_MODE_STA);append(WIFI_EVENT_STA_STOP);append(WIFI_EVENT_STA_START);post_error=77;
    assert(wifi_radio_wait_events_inner(NULL)==77 && posts==1 && !s_radio.event_fence_posted && s_radio.event_phase==RADIO_EVENTS_RESTART);
    post_error=0;assert(wifi_radio_wait_events_inner(NULL)==ESP_OK && posts==2); /* Retry marker, not SDK mutation. */
    reset(WIFI_MODE_STA);wifi_radio_event_fence_t stale={s_radio.event_identity,0};
    assert(wifi_radio_begin_events(RADIO_EVENTS_RESTART,WIFI_MODE_STA)==ESP_OK);
    s_radio.event_fence_posted=true;wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,1,&stale);
    assert(!s_radio.event_fence_seen);cancelled=true;
    assert(wifi_radio_wait_events_inner(NULL)==ESP_ERR_INVALID_STATE);
    reset(WIFI_MODE_STA);ticks=UINT32_MAX-3;
    assert(wifi_radio_wait_events_inner(NULL)==ESP_ERR_TIMEOUT && !posts);
    s_radio.event_identity=UINT32_MAX;
    assert(wifi_radio_begin_events(RADIO_EVENTS_RESTART,WIFI_MODE_STA)==ESP_ERR_NO_MEM && s_radio.restart_required);
    reset(WIFI_MODE_AP);assert(wifi_radio_begin_events(RADIO_EVENTS_STA_START,WIFI_MODE_STA)==ESP_OK);
    append(WIFI_EVENT_STA_START);
    assert(wifi_radio_wait_events_inner(NULL)==ESP_OK && posts==1 && s_radio.event_live==WIFI_MODE_APSTA);
    reset(WIFI_MODE_AP);assert(wifi_radio_begin_events(RADIO_EVENTS_STA_START,WIFI_MODE_STA)==ESP_OK);
    append(WIFI_EVENT_AP_STOP);append(WIFI_EVENT_STA_START);
    assert(wifi_radio_wait_events_inner(NULL)==ESP_ERR_TIMEOUT && !posts); /* STA alone cannot accept APSTA. */
    reset(WIFI_MODE_AP);assert(wifi_radio_begin_events(RADIO_EVENTS_STA_START,WIFI_MODE_STA)==ESP_OK);
    append(WIFI_EVENT_STA_START);late_stop=true;
    assert(wifi_radio_wait_events_inner(NULL)==ESP_OK && posts==2 && s_radio.event_live==WIFI_MODE_APSTA);
    assert(!critical);return 0;
}
