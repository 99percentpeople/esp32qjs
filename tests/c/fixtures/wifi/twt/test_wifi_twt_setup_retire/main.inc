
static void deliver(void) {
    assert(event_queued);event_queued=false;esp32_mquickjs_wifi_twt_setup_result_observe_fence(&queued);
}
static uint32_t begin(int16_t request_id) {
    uint32_t identity=0;assert(esp32_mquickjs_wifi_twt_setup_result_begin_native(request_id,&identity)==0);
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(identity,0);return identity;
}
#define poll(state,token,id) esp32_mquickjs_wifi_twt_setup_retire_poll(state,token,id)
int main(void) {
    uint32_t identity=begin(0);
    esp32_mquickjs_wifi_twt_token_t token={123,7},foreign={124,7};
    esp32_mquickjs_wifi_twt_setup_retire_t state={0};
    cancel_error=71;assert(poll(&state,&token,identity)==71 && !creates && cancels==1);
    assert(poll(&state,&foreign,identity)==ESP_ERR_INVALID_STATE && cancels==1);
    assert(poll(&state,&token,identity+1)==ESP_ERR_INVALID_STATE && cancels==1);
    cancel_error=0;query_error=ESP_ERR_NOT_FINISHED;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && cancels==2 && !creates);
    query_error=0;create_error=72;
    assert(poll(&state,&token,identity)==72 && cancels==2 && state.fence.token.identity);
    create_error=0;hold_callback_exit=true;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && creates==2 && !event_posts);
    fire();assert(poll(&state,&token,identity)==ESP_ERR_TIMEOUT && timer_native.live && !event_posts);
    timer_native.running=false;hold_callback_exit=false;delete_error=73;
    assert(poll(&state,&token,identity)==73 && timer_native.live && state.fence.stopped);
    unsigned stopped=stops;delete_error=0;native_error=74;
    assert(poll(&state,&token,identity)==74 && stops==stopped && !timer_native.live && !event_posts);
    native_error=0;event_error=75;
    assert(poll(&state,&token,identity)==75 && !state.event_sequence);
    event_error=0;assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);
    esp32_mquickjs_wifi_twt_setup_event_fence_t old=queued;
    ++current_cut.timer_revision;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && !state.fence.token.identity && !state.event_sequence);
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && timer_native.live);
    fire();assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);
    esp32_mquickjs_wifi_twt_setup_result_observe_fence(&old);
    assert(!(result(identity).flags&ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_SEEN));
    deliver();release_error=76;
    assert(poll(&state,&token,identity)==76 && !state.released && !timer_native.live);
    release_error=0;assert(poll(&state,&token,identity)==0 && state.released && !state.fence.token.identity);
    unsigned before=releases;assert(poll(&state,&token,identity)==0 && releases==before);
    /* A new native observation after cancellation revokes old proof and
     * requires fresh cancellation/ordering, even if its payload is repeated. */
    identity=begin(1);state=(esp32_mquickjs_wifi_twt_setup_retire_t){0};
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED);fire();
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);old=queued;
    wifi_event_sta_itwt_setup_t event={.config={.twt_id=1,.flow_id=3},.status=1};
    assert(esp32_mquickjs_wifi_twt_setup_result_post(&event,sizeof(event))==0);
    before=cancels;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && cancels==before+1 && timer_native.live);
    esp32_mquickjs_wifi_twt_setup_result_observe_fence(&old);
    fire();assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED && event_queued);
    deliver();assert(poll(&state,&token,identity)==0 && state.released);
    /* Teardown shares the ordering executor. Release failure after removing
     * the result must retry TX only, without reading/releasing that result. */
    identity=begin(2);state=(esp32_mquickjs_wifi_twt_setup_retire_t){0};
    event.config.twt_id=2;assert(esp32_mquickjs_wifi_twt_setup_result_post(&event,sizeof(event))==0);
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_begin_native(identity,3)==0);
    esp32_mquickjs_wifi_twt_setup_result_teardown_submitted_native(identity,0);
    wifi_event_sta_itwt_teardown_t teardown={.flow_id=3,.status=1};
    assert(esp32_mquickjs_wifi_twt_setup_result_teardown_post(&teardown,sizeof(teardown))==0);
    current_cut.information_revision=5;current_cut.teardown_revision=19;
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED);fire();
    assert(poll(&state,&token,identity)==ESP_ERR_NOT_FINISHED);deliver();
    tx_release_error=81;
    assert(poll(&state,&token,identity)==81 && state.result_released && !state.released && tx_releases==1);
    before=releases;unsigned queried=queries;
    tx_release_error=0;assert(poll(&state,&token,identity)==0 && state.released && tx_releases==2);
    assert(releases==before && queries==queried);
    heap_caps_free(s_setup_results.entries);assert(!live && !locked && !timer_native.live);return 0;
}
