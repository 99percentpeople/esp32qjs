
static unsigned posts,forwards,cleanup;
static int post_error;
static uint32_t check_identity;
static esp32_mquickjs_wifi_twt_setup_event_fence_t queued_fence;
static bool fence_early,fence_interleave;
static unsigned fence_posts;
static void deliver(esp32_mquickjs_wifi_twt_setup_event_fence_t value) {
    wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,
        ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_EVENT,&value);
}
static esp32_mquickjs_wifi_twt_setup_result_t read_result(uint32_t id) {
    esp32_mquickjs_wifi_twt_setup_result_t out;
    assert(esp32_mquickjs_wifi_twt_setup_result_read(id,&out)==ESP_OK);return out;
}
static esp32_mquickjs_wifi_twt_setup_results_snapshot_t summary(void) {
    esp32_mquickjs_wifi_twt_setup_results_snapshot_t out;
    esp32_mquickjs_wifi_twt_setup_results_snapshot(&out);return out;
}
static wifi_event_sta_itwt_setup_t event(int16_t request_id,int status) {
    wifi_event_sta_itwt_setup_t value;memset(&value,0xa5,sizeof(value));
    value.config.setup_cmd=TWT_ACCEPT;value.config.trigger=1;value.config.flow_type=1;
    value.config.flow_id=3;value.config.wake_invl_expn=14;value.config.wake_duration_unit=1;
    value.config.reserved=0;value.config.min_wake_dura=10;value.config.wake_invl_mant=1234;
    value.config.twt_id=request_id;value.config.timeout_time_ms=0;
    value.status=status;value.reason=27;value.target_wake_time=UINT64_C(0xfedcba9876543210);return value;
}
static esp_err_t esp_event_post(const char *base,int32_t id,const void *data,size_t size,unsigned ticks) {
    assert(!locked && ticks==0);
    if(base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT) {
        assert(id==ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_EVENT && size==sizeof(queued_fence));
        queued_fence=*(const esp32_mquickjs_wifi_twt_setup_event_fence_t *)data;++fence_posts;
        esp32_mquickjs_wifi_twt_setup_result_t got=read_result(queued_fence.identity);
        assert(got.observation_calls==1 && (got.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_PENDING));
        if(fence_early)deliver(queued_fence);
        got=read_result(queued_fence.identity);
        assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(got.identity,got.revision,queued_fence.sequence));
        uint32_t untouched=123;
        assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(got.identity,got.revision,&untouched)==ESP_ERR_INVALID_STATE && untouched==123);
        if(fence_interleave) {
            wifi_event_sta_itwt_setup_t late=event(got.request_id,1);
            assert(esp32_mquickjs_wifi_twt_setup_result_post(&late,sizeof(late))==post_error);
        }
        return post_error;
    }
    assert(base==WIFI_EVENT);++posts;
    if(id==WIFI_EVENT_ITWT_PROBE){assert(size==8);return post_error;}
    if(id==WIFI_EVENT_ITWT_TEARDOWN) {
        assert(size==8);const uint8_t *bytes=data;assert(!bytes[1] && !bytes[2] && !bytes[3]);
        if(check_identity) {
            esp32_mquickjs_wifi_twt_setup_result_t got=read_result(check_identity);
            assert((got.flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN) && got.observation_calls==1);
            assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(check_identity,got.revision,got.fence_sequence));
        }
        return post_error;
    }
    assert(id==WIFI_EVENT_ITWT_SETUP && size==32);
    const wifi_event_sta_itwt_setup_t *value=data;const uint8_t *bytes=data;
    assert(bytes[7]==0 && bytes[14]==0 && bytes[15]==0 && bytes[21]==0 && bytes[22]==0 && bytes[23]==0);
    if(value->status==1)assert(!value->reason);
    if(check_identity) {
        esp32_mquickjs_wifi_twt_setup_result_t got=read_result(check_identity);
        assert((got.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN) && got.observation_calls==1 && !cleanup);
        assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(check_identity,got.revision,got.fence_sequence));
    }
    return post_error;
}
int __real_wifi_event_post(int id,void *data,size_t size) {
    assert(!locked && id==987 && !data && !size);++forwards;return 78;
}
static void cold_boot(void) {
    heap_caps_free(s_setup_results.entries);memset(&s_setup_results,0,sizeof(s_setup_results));
    s_setup_results.snapshot.last_request_id=-1;
    allocations=posts=forwards=cleanup=fence_posts=0;fence_early=fence_interleave=false;allocation_error=false;post_error=0;check_identity=0;
}
static void fence_and_release(uint32_t identity) {
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(identity,0));
    esp32_mquickjs_wifi_twt_setup_result_t got=read_result(identity);
    uint32_t sequence=0;
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(identity,got.revision,&sequence)==ESP_OK && sequence);
    got=read_result(identity);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(identity,got.revision,sequence));
    deliver(queued_fence);got=read_result(identity);
    assert(esp32_mquickjs_wifi_twt_setup_result_release_native(identity,got.revision,sequence));
}
static void test_fences(void) {
    cold_boot();uint32_t owner=0,other=0,sequence=111;
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(0,&owner)==0);
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(1,&other)==0);
    esp32_mquickjs_wifi_twt_setup_result_t got=read_result(owner);
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision,&sequence)==ESP_ERR_INVALID_STATE && sequence==111);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(owner,0);
    got=read_result(owner);
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision,&sequence)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(owner,8));
    got=read_result(owner);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(owner,got.revision,1));
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision-1,&sequence)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(0,got.revision,&sequence)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision,NULL)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision,&sequence)==0 && sequence==1);
    esp32_mquickjs_wifi_twt_setup_event_fence_t old=queued_fence;
    got=read_result(owner);uint32_t revision=got.revision;
    deliver((esp32_mquickjs_wifi_twt_setup_event_fence_t){other,sequence});
    deliver((esp32_mquickjs_wifi_twt_setup_event_fence_t){owner,sequence+1});
    wifi_radio_lifecycle_fence(NULL,WIFI_EVENT,5,&old);
    wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,5,NULL);
    assert(read_result(owner).revision==revision && !read_result(other).fence_sequence);
    deliver(old);got=read_result(owner);
    assert(got.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(owner,revision,sequence));
    revision=got.revision;deliver(old);assert(read_result(owner).revision==revision);
    /* A new observation invalidates the delivered fence, including duplicates. */
    wifi_event_sta_itwt_setup_t value=event(0,1);
    assert(esp32_mquickjs_wifi_twt_setup_result_post(&value,sizeof(value))==0);
    got=read_result(owner);assert(!(got.flags&SETUP_FENCE_FLAGS));
    deliver(old);assert(read_result(owner).revision==got.revision);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(owner,got.revision,sequence));
    /* Even an early callback cannot authorize release if posting then fails. */
    assert(!(got.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED) && got.cancelled_flows==8);
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(owner,16));got=read_result(owner);
    assert(got.cancelled_flows==24);
    fence_early=true;post_error=79;sequence=111;
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision,&sequence)==79 && sequence==111);
    got=read_result(owner);assert(!(got.flags&SETUP_FENCE_FLAGS) && !got.observation_calls);
    assert(!got.observation_error); /* Fence failure is not native observation failure. */
    esp32_mquickjs_wifi_twt_setup_event_fence_t failed=queued_fence;
    post_error=0;fence_interleave=true;
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision,&sequence)==ESP_ERR_NOT_FINISHED && sequence==111);
    got=read_result(owner);assert(!(got.flags&SETUP_FENCE_FLAGS) && !got.observation_calls);
    fence_interleave=false;fence_early=false;
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(owner,0));got=read_result(owner);
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(owner,got.revision,&sequence)==0 && sequence==4);
    got=read_result(owner);revision=got.revision;deliver(old);deliver(failed);
    assert(read_result(owner).revision==revision);
    deliver(queued_fence);got=read_result(owner);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(owner,got.revision,sequence-1));
    old=queued_fence;assert(esp32_mquickjs_wifi_twt_setup_result_release_native(owner,got.revision,sequence));
    uint32_t next=0;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(2,&next)==0);
    got=read_result(next);deliver(old);assert(read_result(next).revision==got.revision && !got.fence_sequence);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(next,0);fence_early=true;
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(next,0));
    got=read_result(next);assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(next,got.revision,&sequence)==0 && sequence==1);
    got=read_result(next);assert(esp32_mquickjs_wifi_twt_setup_result_release_native(next,got.revision,sequence));
    /* Exhaustion never recycles a sequence within the same live identity. */
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(other,0);
    assert(esp32_mquickjs_wifi_twt_setup_result_cancelled_native(other,0));
    s_setup_results.entries[1].fence_sequence=UINT32_MAX;got=read_result(other);sequence=111;
    assert(esp32_mquickjs_wifi_twt_setup_result_post_fence(other,got.revision,&sequence)==ESP_ERR_NO_MEM && sequence==111);
    assert(read_result(other).fence_sequence==UINT32_MAX);
    cold_boot();
}
static void test_teardown(void) {
    cold_boot();uint32_t owner=0,other=0;
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(0,&owner)==0);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(owner,3)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(owner,0);
    wifi_event_sta_itwt_setup_t setup=event(0,1);
    assert(__wrap_wifi_event_post(28,&setup,sizeof(setup))==0);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(owner,2)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(owner,8)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(owner,3)==0);
    assert(!esp32_mquickjs_wifi_twt_setup_result_cancelled_native(owner,8));
    /* High teardown flags survive a subsequent native setup observation. */
    assert(__wrap_wifi_event_post(28,&setup,sizeof(setup))==0);
    assert(read_result(owner).flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING);
    wifi_event_sta_itwt_teardown_t teardown;memset(&teardown,0xa5,sizeof(teardown));
    teardown.flow_id=3;teardown.status=ITWT_TEARDOWN_SUCCESS;
    check_identity=owner;post_error=79;
    assert(__wrap_wifi_event_post(29,&teardown,sizeof(teardown))==79);check_identity=0;
    esp32_mquickjs_wifi_twt_setup_result_t got=read_result(owner);
    assert(got.teardown_status==ITWT_TEARDOWN_SUCCESS && got.teardown_observation_error==79 && !got.observation_error && !got.observation_calls);
    esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(owner,82);
    got=read_result(owner);assert(got.teardown_error==82 && !got.submit_error && !(got.flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SUBMITTING));
    assert(got.flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(owner,3)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(owner,0);assert(read_result(owner).teardown_error==82);
    post_error=0;teardown.status=ITWT_TEARDOWN_FAIL;
    assert(__wrap_wifi_event_post(29,&teardown,sizeof(teardown))==0);
    got=read_result(owner);assert(got.teardown_status==ITWT_TEARDOWN_SUCCESS && (got.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS));
    uint32_t revision=got.revision;teardown.flow_id=8;
    assert(__wrap_wifi_event_post(29,&teardown,sizeof(teardown))==0 && read_result(owner).revision==revision);
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(1,&other)==0);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(other,0);setup=event(1,1);
    assert(__wrap_wifi_event_post(28,&setup,sizeof(setup))==0);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(other,3)==ESP_ERR_INVALID_STATE);
    assert(!(read_result(other).flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_ATTEMPTED));
    unsigned posted=posts;teardown.flow_id=9;
    assert(__wrap_wifi_event_post(29,&teardown,sizeof(teardown))==ESP_ERR_INVALID_ARG && posts==posted);
    teardown.flow_id=3;teardown.status=(wifi_itwt_teardown_status_t)8;
    assert(__wrap_wifi_event_post(29,&teardown,sizeof(teardown))==ESP_ERR_INVALID_ARG && posts==posted);
    assert(__wrap_wifi_event_post(29,NULL,8)==ESP_ERR_INVALID_ARG);
    cold_boot();
}
static void test_connection_close(void) {
    cold_boot();
    uint32_t accepted=0,queued=0;
    assert(!esp32_mquickjs_wifi_twt_setup_result_begin_native(40,&accepted));
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(accepted,0);
    wifi_event_sta_itwt_setup_t value=event(40,1);
    assert(!esp32_mquickjs_wifi_twt_setup_result_post(&value,sizeof(value)));
    assert(!esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(accepted,3));
    esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(accepted,81);
    assert(!esp32_mquickjs_wifi_twt_setup_result_begin_native(41,&queued));
    esp32_mquickjs_wifi_twt_setup_results_connection_closed_native();
    assert(esp32_mquickjs_wifi_twt_setup_request_closed_native(40));
    assert(esp32_mquickjs_wifi_twt_setup_request_closed_native(41));
    assert(!esp32_mquickjs_wifi_twt_setup_request_closed_native(42));
    esp32_mquickjs_wifi_twt_setup_result_t a=read_result(accepted),q=read_result(queued);
    assert(a.teardown_error==81 && !(a.flags&ESP32_MQUICKJS_WIFI_TWT_TEARDOWN_SEEN));
    assert(!(a.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED));
    assert(q.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING);
    assert(!esp32_mquickjs_wifi_twt_setup_result_cancelled_native(queued,0));
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(accepted,a.revision,1));
    esp32_mquickjs_wifi_twt_setup_results_connection_closed_native();
    assert(read_result(accepted).revision==a.revision); /* Idempotent revocation. */
    /* Late successful setup cannot regain association authority. */
    value=event(41,1);assert(!esp32_mquickjs_wifi_twt_setup_result_post(&value,sizeof(value)));
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(queued,0);
    assert(esp32_mquickjs_wifi_twt_setup_request_closed_native(41));
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(queued,3)==ESP_ERR_INVALID_STATE);
    fence_and_release(accepted);fence_and_release(queued);
    uint32_t next=0;assert(!esp32_mquickjs_wifi_twt_setup_result_begin_native(42,&next));
    assert(!esp32_mquickjs_wifi_twt_setup_request_closed_native(42));
    cold_boot();
}
static void test_close_consent(void) {
    cold_boot();uint32_t first=0,second=0;
    assert(!esp32_mquickjs_wifi_twt_setup_result_begin_native(20,&first));
    assert(!esp32_mquickjs_wifi_twt_setup_result_begin_native(21,&second));
    assert(!esp32_mquickjs_wifi_twt_setup_request_closing(20));
    esp32_mquickjs_wifi_twt_setup_result_request_close(first);
    esp32_mquickjs_wifi_twt_setup_result_t before=read_result(first);
    assert(before.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING);
    assert(!(before.flags&(ESP32_MQUICKJS_WIFI_TWT_SETUP_NATIVE_CLOSED|ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED)));
    assert(esp32_mquickjs_wifi_twt_setup_request_closing(20));
    assert(!esp32_mquickjs_wifi_twt_setup_request_closing(21) && !esp32_mquickjs_wifi_twt_setup_request_closing(-1));
    esp32_mquickjs_wifi_twt_setup_result_request_close(first);
    assert(read_result(first).revision==before.revision);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(first,0);
    wifi_event_sta_itwt_setup_t value=event(20,1);
    assert(!esp32_mquickjs_wifi_twt_setup_result_post(&value,sizeof(value)));
    assert(esp32_mquickjs_wifi_twt_setup_request_closing(20));
    assert(!esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(first,3));
    esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(first,-51);
    assert(esp32_mquickjs_wifi_twt_setup_request_closing(20) && read_result(first).teardown_error==-51);
    cold_boot();
}
int main(void) {
    cold_boot();wifi_event_sta_itwt_setup_t value=event(0,1);
    assert(__wrap_wifi_event_post(28,&value,sizeof(value))==ESP_OK && posts==1 && !allocations);
    uint32_t ids[8]={0};
    for(int16_t i=0;i<8;++i)assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(i,&ids[i])==ESP_OK);
    assert(allocations==1 && live==1 && summary().owned_mask==255 && summary().reserved_bytes==576);
    uint32_t rejected=0;
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(8,&rejected)==ESP_ERR_WIFI_TWT_FULL && !rejected);
    assert(summary().last_request_id==7 && summary().last_identity==8);
    check_identity=ids[0];post_error=79;
    assert(__wrap_wifi_event_post(28,&value,sizeof(value))==79);++cleanup;check_identity=0;
    esp32_mquickjs_wifi_twt_setup_result_t first=read_result(ids[0]);
    assert((first.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING) && first.observation_error==79 && !first.observation_calls);
    assert(first.event.target_wake_time==value.target_wake_time && first.event.config.twt_id==0 && !first.event.reason);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(ids[0],first.revision,first.fence_sequence));
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(ids[0],80); /* event before SDK error */
    first=read_result(ids[0]);assert(first.submit_error==80 && (first.flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN));
    assert(!(first.flags&(ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTING|ESP32_MQUICKJS_WIFI_TWT_SETUP_SUBMITTED)));
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(ids[0],0);assert(read_result(ids[0]).submit_error==80);
    post_error=0;assert(__wrap_wifi_event_post(28,&value,sizeof(value))==0);
    assert(!(read_result(ids[0]).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS));
    value.status=0x301a;assert(__wrap_wifi_event_post(28,&value,sizeof(value))==0);
    assert((read_result(ids[0]).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS) && read_result(ids[0]).event.status==1);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(ids[0],first.revision,first.fence_sequence));
    fence_and_release(ids[0]);
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(ids[0],first.revision,first.fence_sequence));
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(0,&rejected)==ESP_ERR_INVALID_ARG && !rejected);
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(8,&rejected)==ESP_OK && rejected==9);
    value=event(0,1);assert(__wrap_wifi_event_post(28,&value,sizeof(value))==0);
    assert(!(read_result(rejected).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_SEEN)); /* old event/new slot */
    value=event(8,-99);assert(__wrap_wifi_event_post(28,&value,sizeof(value))==0);
    assert(read_result(rejected).event.status==-99 && read_result(rejected).event.reason==27);
    esp32_mquickjs_wifi_twt_setup_result_t untouched;memset(&untouched,0x55,sizeof(untouched));
    esp32_mquickjs_wifi_twt_setup_result_t before=untouched;
    assert(esp32_mquickjs_wifi_twt_setup_result_read(ids[0],&untouched)==ESP_ERR_INVALID_STATE && !memcmp(&before,&untouched,sizeof(before)));
    assert(__wrap_wifi_event_post(987,NULL,0)==78 && forwards==1);
    uint32_t probe=0;assert(esp32_mquickjs_wifi_twt_probe_result_begin_native(&probe)==0);
    wifi_event_sta_itwt_probe_t probe_event={ITWT_PROBE_SUCCESS,88};
    assert(__wrap_wifi_event_post(30,&probe_event,sizeof(probe_event))==0 && s_probe_result.event_seen && !s_probe_result.event.reason);
    cold_boot();allocation_error=true;rejected=0;
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(0,&rejected)==ESP_ERR_NO_MEM && !rejected && !live);
    allocation_error=false;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(0,&rejected)==0);
    assert(summary().last_identity==1 && summary().last_request_id==0);
    unsigned before_posts=posts;
    assert(__wrap_wifi_event_post(28,NULL,32)==ESP_ERR_INVALID_ARG && posts==before_posts);
    assert(summary().fault==ESP_ERR_INVALID_ARG);
    uint32_t blocked=0;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(1,&blocked)==ESP_ERR_INVALID_ARG && !blocked);
    cold_boot();s_setup_results.snapshot.last_identity=UINT32_MAX;blocked=0;
    assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(0,&blocked)==ESP_ERR_NO_MEM && !blocked);
    cold_boot();blocked=0;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(INT16_MAX,&blocked)==0);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(blocked,0);
    fence_and_release(blocked);
    blocked=0;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(INT16_MAX,&blocked)==ESP_ERR_INVALID_ARG);
    cold_boot();blocked=0;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(0,&blocked)==0);
    s_setup_results.entries[0].revision=UINT32_MAX;
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(blocked,0);
    assert(summary().fault==ESP_ERR_NO_MEM && (read_result(blocked).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_AMBIGUOUS));
    assert(!esp32_mquickjs_wifi_twt_setup_result_release_native(blocked,UINT32_MAX,1));
    cold_boot();test_fences();test_teardown();test_connection_close();test_close_consent();assert(!live && !locked);return 0;
}
