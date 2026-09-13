
int main(void){
    esp32_mquickjs_wifi_twt_information_identity_t native={.node=17,.flows=8,.control=0x63};
    native.request_ids[3]=123;
    esp32_mquickjs_wifi_twt_information_result_t result;
    uint32_t id=0,other=0;
    fail_allocate=true;
    assert(esp32_mquickjs_wifi_twt_information_begin_native(10,&native,120,false,&id)==ESP_ERR_NO_MEM && !id);
    fail_allocate=false;
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(10,&native,120,false,&id) && id==1);
    assert(esp32_mquickjs_wifi_twt_information_begin_native(11,&native,120,false,&other)==ESP_ERR_INVALID_STATE && !other);
    assert(esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,120));
    assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,1,120));
    task=(void *)99;assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,120));task=(void *)31;
    esp32_mquickjs_wifi_twt_information_bind_native(&native,42);
    assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,120));
    assert(!esp32_mquickjs_wifi_twt_information_callback_begin(41));
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(42));
    wifi_event_sta_itwt_suspend_t event;memset(&event,0xa5,sizeof(event));event.status=0;event.flow_id_bitmap=8;
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)) && !posts);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && !result.complete);
    current_identity=id;inspect_post=true;post_error=0x107;
    esp32_mquickjs_wifi_twt_information_callback_end(42,true);
    assert(posts==1 && esp32_mquickjs_wifi_twt_information_read(id,&result) && result.complete &&
        !result.native_error && result.observation_error==post_error && result.submitting);
    assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==ESP_ERR_NOT_FINISHED);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    tx_error=ESP_ERR_NOT_FINISHED;assert(esp32_mquickjs_wifi_twt_information_reap_native(0)==tx_error);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.cleanup_error==tx_error);
    tx_error=0;assert(!esp32_mquickjs_wifi_twt_information_reap_native(0));
    assert(!esp32_mquickjs_wifi_twt_information_read(id,&result) && !esp32_mquickjs_wifi_twt_information_pending());
    inspect_post=false;post_error=0;
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(11,&native,120,false,&other) && other>id);
    esp32_mquickjs_wifi_twt_information_abandon(id); /* Old identity cannot abandon the new operation. */
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result) && !result.abandoned);
    assert(!esp32_mquickjs_wifi_twt_information_callback_begin(42));
    event.status=ESP_ERR_NO_MEM;event.flow_id_bitmap=0;
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)) && posts==1);
    esp32_mquickjs_wifi_twt_information_submitted_native(other,ESP_ERR_NO_MEM);
    assert(posts==2 && esp32_mquickjs_wifi_twt_information_read(other,&result) && result.complete);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(10)); /* Another Agreement is unaffected. */
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result));
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(11));
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(12,&native,120,false,&id));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,43);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(43));
    event.status=0;event.flow_id_bitmap=8;
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)));
    timer_error=ESP_ERR_NO_MEM;esp32_mquickjs_wifi_twt_information_callback_end(43,true);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.native_error==ESP_ERR_NO_MEM);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(12));
    /* Resume waits for its replacement timer, not merely successful TX. */
    timer_error=0;
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(13,&native,0,true,&id));
    assert(esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,3,0,0));
    assert(!esp32_mquickjs_wifi_twt_information_producer_allowed((void *)17,3,0,0,0));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,44);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(44));
    esp32_mquickjs_wifi_twt_information_timer_bound_native(&native,100);
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)));
    esp32_mquickjs_wifi_twt_information_callback_end(44,true);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.tx_complete && !result.complete);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,99,0);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && !result.complete);
    task=(void *)99;esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,100,0);task=(void *)31;
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && !result.complete);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,100,0);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.complete && result.resume_complete);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(13));
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(14,&native,0,true,&other));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,45);
    esp32_mquickjs_wifi_twt_information_submitted_native(other,0);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,100,0); /* Old numeric timer. */
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result) && !result.complete);
    resume_allowed=false;esp32_mquickjs_wifi_twt_information_refresh_native();
    assert(esp32_mquickjs_wifi_twt_information_read(other,&result) && result.complete && result.native_error==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(14));resume_allowed=true;
    /* A successful native observation must not erase an earlier timer error. */
    assert(!esp32_mquickjs_wifi_twt_information_begin_native(15,&native,0,true,&id));
    esp32_mquickjs_wifi_twt_information_bind_native(&native,46);
    esp32_mquickjs_wifi_twt_information_submitted_native(id,0);
    assert(esp32_mquickjs_wifi_twt_information_callback_begin(46));
    esp32_mquickjs_wifi_twt_information_timer_bound_native(&native,101);
    esp32_mquickjs_wifi_twt_information_timer_finished_native(&native,101,ESP_ERR_NO_MEM);
    assert(!esp32_mquickjs_wifi_twt_information_post(&event,sizeof(event)));
    esp32_mquickjs_wifi_twt_information_callback_end(46,true);
    assert(esp32_mquickjs_wifi_twt_information_read(id,&result) && result.complete && result.native_error==ESP_ERR_NO_MEM);
    assert(!esp32_mquickjs_wifi_twt_information_reap_native(15));
    unsigned before=allocations;
    assert(esp32_mquickjs_wifi_twt_information_begin_native(13,&native,4294968U,false,&id)==ESP_ERR_INVALID_ARG);
    s_information.last_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_twt_information_begin_native(13,&native,120,false,&id)==ESP_ERR_NO_MEM);
    assert(allocations==before && !locked);free(s_information.operation);return 0;
}
