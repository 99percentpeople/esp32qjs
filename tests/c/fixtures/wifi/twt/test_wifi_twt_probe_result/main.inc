
static unsigned posts,forwards,cleanup,fence_posts;
static esp32_mquickjs_wifi_twt_probe_event_fence_t queued_fence;
static int post_error;
static bool check_captured,try_begin;
static void (*post_hook)(void);
static esp32_mquickjs_wifi_twt_probe_result_snapshot_t snapshot(void) {
    esp32_mquickjs_wifi_twt_probe_result_snapshot_t s;
    esp32_mquickjs_wifi_twt_probe_result_snapshot(&s);return s;
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
    if(base==WIFI_EVENT && id==31) {
        assert(!locked && ticks==0 && size==40);
        const wifi_event_sta_itwt_suspend_t *event=data;const uint8_t *bytes=data;
        assert(event->status==81 && event->flow_id_bitmap==8 && event->actual_suspend_time_ms[3]==120);
        assert(!bytes[5] && !bytes[6] && !bytes[7]);
        for(unsigned i=0;i<8;++i)if(i!=3)assert(!event->actual_suspend_time_ms[i]);
        return post_error;
    }
    assert(!locked && ticks==0 && size==8);
    if(base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT) {
        assert(id==ESP32_MQUICKJS_WIFI_TWT_PROBE_FENCE_EVENT);++fence_posts;
        queued_fence=*(const esp32_mquickjs_wifi_twt_probe_event_fence_t *)data;return post_error;
    }
    assert(base==WIFI_EVENT && id==30);++posts;
    const wifi_event_sta_itwt_probe_t *event=data;
    const uint8_t *bytes=data;
    assert(bytes[5]==0 && bytes[6]==0 && bytes[7]==0);
    if(event->status==ITWT_PROBE_SUCCESS)assert(event->reason==0);
    if(check_captured) {
        assert(snapshot().event_seen && snapshot().event.status==event->status);
        assert(snapshot().observation_calls==1 && cleanup==0);
    }
    if(try_begin) {
        uint32_t id=77;assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&id)==ESP_ERR_INVALID_STATE && id==77);
        assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(snapshot().identity)==ESP_ERR_INVALID_STATE);
    }
    if(post_hook)post_hook();
    return post_error;
}
int __real_wifi_event_post(int id,void *data,size_t size) {
    assert(!locked && id==12 && data==(void *)23 && size==99);++forwards;return -81;
}
static uint32_t begin(void) {
    uint32_t id=0;assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&id)==ESP_OK && id);
    return id;
}
static void cold_boot(void) {
    memset(&s_probe_result,0,sizeof(s_probe_result));
    posts=forwards=cleanup=0;post_error=0;check_captured=try_begin=false;post_hook=NULL;
}
static void duplicate(void) {
    post_hook=NULL;wifi_event_sta_itwt_probe_t e={.status=ITWT_PROBE_FAIL,.reason=9};
    check_captured=false;assert(__wrap_wifi_event_post(30,&e,sizeof(e))==post_error);
}
int main(void) {
    wifi_event_sta_itwt_suspend_t information;memset(&information,0xa5,sizeof(information));
    information.status=81;information.flow_id_bitmap=8;information.actual_suspend_time_ms[3]=120;post_error=0x107;
    assert(__wrap_wifi_event_post(31,&information,sizeof(information))==0x107);
    assert(__wrap_wifi_event_post(31,NULL,sizeof(information))==ESP_ERR_INVALID_ARG);
    assert(__wrap_wifi_event_post(31,&information,sizeof(information)-1)==ESP_ERR_INVALID_ARG);
    post_error=0;
    assert(__wrap_wifi_event_post(12,(void *)23,99)==-81 && forwards==1 && posts==0);
    wifi_event_sta_itwt_probe_t e;memset(&e,0xa7,sizeof(e));e.status=ITWT_PROBE_SUCCESS;
    /* Unowned observations cannot invent an operation identity. */
    assert(__wrap_wifi_event_post(30,&e,sizeof(e))==0 && !snapshot().identity && !snapshot().event_seen);
    cold_boot();uint32_t first=begin(),unchanged=71;
    assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&unchanged)==ESP_ERR_INVALID_STATE && unchanged==71);
    check_captured=try_begin=true;post_error=0x107;
    assert(__wrap_wifi_event_post(30,&e,sizeof(e))==post_error);++cleanup;
    assert(cleanup==1 && snapshot().event_seen && snapshot().submitting);
    assert(snapshot().observation_error==0x107 && snapshot().event.reason==0 && !snapshot().observation_calls);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(first,0);
    assert(snapshot().submitted && !snapshot().submitting && snapshot().submit_error==0);
    check_captured=false;post_error=0;e.status=ITWT_PROBE_TIMEOUT;e.reason=18;
    assert(__wrap_wifi_event_post(30,&e,sizeof(e))==0);
    assert(snapshot().event.status==ITWT_PROBE_SUCCESS && snapshot().observation_error==0x107);
    /* The wrapper, not this record, separately proves native admission. */
    uint32_t second=begin();assert(second==first+1 && !snapshot().event_seen);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(first,-91);
    assert(snapshot().submitting && snapshot().submit_error==0);
    cleanup=0;check_captured=true;post_error=ESP_ERR_NO_MEM;
    assert(__wrap_wifi_event_post(30,&e,sizeof(e))==ESP_ERR_NO_MEM);++cleanup;
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(second,-17);
    assert(snapshot().submit_error==-17 && !snapshot().submitted && snapshot().event.reason==18);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(second,0);assert(snapshot().submit_error==-17);
    cold_boot();first=begin();esp32_mquickjs_wifi_twt_probe_result_failed_native(ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_twt_probe_result_failed_native(-88);
    assert(snapshot().control_error==ESP_ERR_NO_MEM && !snapshot().event_seen && posts==0);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(first,ESP_ERR_NO_MEM);
    assert(snapshot().submit_error==ESP_ERR_NO_MEM && !snapshot().submitted);
    cold_boot();first=begin();post_hook=duplicate;
    e.status=ITWT_PROBE_SUCCESS;assert(__wrap_wifi_event_post(30,&e,sizeof(e))==0);
    assert(posts==2 && snapshot().event.status==ITWT_PROBE_SUCCESS && snapshot().observation_calls==0);
    cold_boot();first=begin();e.status=(wifi_itwt_probe_status_t)4;
    assert(__wrap_wifi_event_post(30,&e,sizeof(e))==ESP_ERR_INVALID_ARG && posts==0 && !snapshot().event_seen);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(first,-1);
    assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&first)==ESP_ERR_INVALID_ARG);
    cold_boot();assert(__wrap_wifi_event_post(30,NULL,sizeof(e))==ESP_ERR_INVALID_ARG && posts==0);
    cold_boot();assert(__wrap_wifi_event_post(30,&e,sizeof(e)-1)==ESP_ERR_INVALID_ARG && posts==0);
    cold_boot();s_probe_result.identity=UINT32_MAX-1;first=begin();assert(first==UINT32_MAX);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(first,0);
    assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&first)==ESP_ERR_NO_MEM);
    assert(snapshot().identity==UINT32_MAX && snapshot().fault==ESP_ERR_NO_MEM);
    assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(NULL)==ESP_ERR_INVALID_ARG);
    cold_boot();first=begin();
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(0)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(first)==ESP_ERR_INVALID_STATE);
    assert(!snapshot().cancel_requested);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(first,0);
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(first+1)==ESP_ERR_INVALID_STATE);
    assert(!snapshot().cancel_requested);
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(first)==ESP_OK);
    assert(snapshot().cancel_requested && !snapshot().cancel_complete);
    unchanged=91;
    assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&unchanged)==ESP_ERR_INVALID_STATE && unchanged==91);
    e.status=ITWT_PROBE_SUCCESS;
    assert(__wrap_wifi_event_post(30,&e,sizeof(e))==0 && !snapshot().event_seen && posts==1);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(first,81);
    assert(snapshot().cancel_error==81 && !snapshot().cancel_complete);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(first+1,0);
    assert(snapshot().cancel_error==81 && !snapshot().cancel_complete);
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(first)==ESP_OK);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(first,0);
    assert(snapshot().cancel_complete && snapshot().cancel_error==0 && !snapshot().event_seen);
    second=begin();assert(second==first+1 && !snapshot().cancel_requested && !snapshot().cancel_complete);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(first,82);
    assert(snapshot().cancel_error==0 && !snapshot().cancel_complete);
    /* Native completion captured before cancellation remains the winner. */
    assert(__wrap_wifi_event_post(30,&e,sizeof(e))==0 && snapshot().event_seen);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(second,0);
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(second)==ESP_OK);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(second,0);
    assert(snapshot().cancel_complete && snapshot().event.status==ITWT_PROBE_SUCCESS);
    cold_boot();first=begin();
    assert(!esp32_mquickjs_wifi_twt_probe_result_claim_native(first));
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(first,0);
    assert(esp32_mquickjs_wifi_twt_probe_result_claim_native(first) && snapshot().owned);
    assert(!esp32_mquickjs_wifi_twt_probe_result_claim_native(first));
    assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&unchanged)==ESP_ERR_INVALID_STATE);
    uint32_t sequence=91;
    assert(esp32_mquickjs_wifi_twt_probe_result_post_fence(first,&sequence)==ESP_ERR_INVALID_STATE && sequence==91);
    assert(esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(first)==ESP_OK);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(first,0);
    post_error=81;
    assert(esp32_mquickjs_wifi_twt_probe_result_post_fence(first,&sequence)==81 && sequence==91);
    assert(!snapshot().fence_pending && snapshot().fence_sequence==1 && snapshot().owned);
    post_error=0;
    assert(esp32_mquickjs_wifi_twt_probe_result_post_fence(first,&sequence)==ESP_OK && sequence==2);
    assert(snapshot().fence_pending && !snapshot().event_fenced);
    assert(!esp32_mquickjs_wifi_twt_probe_result_release_native(first,sequence));
    esp32_mquickjs_wifi_twt_probe_event_fence_t stale=queued_fence;--stale.sequence;
    esp32_mquickjs_wifi_twt_probe_result_observe_fence(&stale);assert(!snapshot().event_fenced);
    stale=queued_fence;++stale.identity;
    esp32_mquickjs_wifi_twt_probe_result_observe_fence(&stale);assert(!snapshot().event_fenced);
    esp32_mquickjs_wifi_twt_probe_result_observe_fence(&queued_fence);
    assert(snapshot().event_fenced && !snapshot().fence_pending && snapshot().owned);
    assert(!esp32_mquickjs_wifi_twt_probe_result_release_native(first,sequence-1));
    assert(esp32_mquickjs_wifi_twt_probe_result_release_native(first,sequence) && !snapshot().owned);
    assert(!esp32_mquickjs_wifi_twt_probe_result_release_native(first,sequence));
    second=begin();esp32_mquickjs_wifi_twt_probe_result_observe_fence(&queued_fence);
    assert(!snapshot().event_fenced && snapshot().identity==second);
    esp32_mquickjs_wifi_twt_probe_result_submitted_native(second,0);
    assert(esp32_mquickjs_wifi_twt_probe_result_claim_native(second));
    esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(second);
    esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(second,0);
    s_probe_result.fence_sequence=UINT32_MAX;
    assert(esp32_mquickjs_wifi_twt_probe_result_post_fence(second,&sequence)==ESP_ERR_NO_MEM && snapshot().owned);
    return 0;
}
