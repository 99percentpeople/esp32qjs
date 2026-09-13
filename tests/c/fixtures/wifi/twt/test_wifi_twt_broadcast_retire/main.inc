
static unsigned block_stops,native_fences,cancels,queries,releases;
static esp_err_t block_error,native_error,cancel_error,query_error,release_error;
esp_err_t esp_timer_stop_blocking(esp_timer_handle_t handle,unsigned ticks) {
    struct timer_record *timer=handle;assert(!locked && timer->live && ticks==1);++block_stops;
    if(block_error)return block_error;timer->active=false;return 0;
}
esp_err_t esp32_mquickjs_wifi_action_sdk_fence(void){assert(!locked);++native_fences;return native_error;}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_cancel(unsigned slot,uint32_t identity) {
    assert(!locked);++cancels;if(cancel_error)return cancel_error;
    return esp32_mquickjs_wifi_btwt_setup_cancel_native(slot,identity);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent(unsigned slot,uint32_t identity,esp32_mquickjs_wifi_btwt_cut_t *out) {
    assert(!locked);++queries;if(query_error)return query_error;
    return esp32_mquickjs_wifi_btwt_setup_quiescent_native(slot,identity,out);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_release(unsigned slot,uint32_t identity,const esp32_mquickjs_wifi_btwt_cut_t *cut,uint32_t sequence) {
    assert(!locked && native_fences);++releases;if(release_error)return release_error;
    return esp32_mquickjs_wifi_btwt_setup_release_native(slot,identity,cut,sequence);
}
#define poll(state,token,id) esp32_mquickjs_wifi_btwt_retire_poll(state,token,3,id)
static void close_seed(uint32_t identity,esp_err_t submitted) {
    reset();assert(!s_teardown_tx.state.identity);
    block_error=native_error=cancel_error=query_error=release_error=0;
    block_stops=native_fences=cancels=queries=releases=pm_releases=0;
    assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,identity,current[3].node,current[3].parameter));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(3,identity));
    s_btwt_timer.entries[3].result.complete=true;s_btwt_timer.entries[3].result.seen=true;
    s_btwt_timer.entries[3].result.event.status=BTWT_SETUP_SUCCESS;
    assert(!esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(identity,current[3].node,3));
    esp32_mquickjs_wifi_twt_teardown_wake_up_native();
    /* Seed the SDK-output/completion boundary. Actual output/callback/EB
     * validation uses production code in test_wifi_twt_teardown_tx.py. */
    s_teardown_tx.buffer=(void *)44;s_teardown_tx.state.output_seen=true;
    s_teardown_tx.state.output_returned=true;s_teardown_tx.state.completion_seen=true;
    s_teardown_tx.state.completion_status=BTWT_TEARDOWN_SUCCESS;
    s_teardown_tx.state.observation_error=83;
    assert(!esp32_mquickjs_wifi_twt_teardown_tx_end_native(identity,submitted));
}
static void close_recycle(uint32_t identity) {
    esp32_mquickjs_wifi_twt_teardown_tx_recycle((void *)44,identity,true);
    esp32_mquickjs_wifi_twt_teardown_tx_recycle((void *)44,identity,false);
}
static void close_finish(esp32_mquickjs_wifi_btwt_retire_t *state,const esp32_mquickjs_wifi_twt_token_t *token,uint32_t identity) {
    assert(poll(state,token,identity)==ESP_ERR_NOT_FINISHED && state->fence.timer);
    fire(state->fence.timer);early_fence=true;
    assert(!poll(state,token,identity) && state->released && !s_teardown_tx.state.identity);
    assert(!esp32_mquickjs_wifi_btwt_setup_held(3));
}
static void teardown_retire_cases(void) {
    esp32_mquickjs_wifi_twt_token_t token={125,7};
    esp32_mquickjs_wifi_btwt_retire_t state={0};
    close_seed(201,0);
    assert(poll(&state,&token,201)==ESP_ERR_NOT_FINISHED && !creates && !pm_releases);
    close_recycle(201);assert(!pm_releases);
    assert(poll(&state,&token,201)==ESP_ERR_NOT_FINISHED && state.fence.timer && pm_releases==1);
    assert(state.cut.teardown_revision && s_teardown_tx.state.identity==201);
    fire(state.fence.timer);assert(poll(&state,&token,201)==ESP_ERR_NOT_FINISHED && state.sequence);
    esp32_mquickjs_wifi_btwt_event_fence_t old=saved_fence;
    esp32_mquickjs_wifi_twt_teardown_tx_output_returned((void *)44,201);
    assert(poll(&state,&token,201)==ESP_ERR_NOT_FINISHED && !state.fence.token.identity);
    assert(s_teardown_tx.state.identity==201 && esp32_mquickjs_wifi_btwt_setup_held(3));
    assert(poll(&state,&token,201)==ESP_ERR_NOT_FINISHED && state.fence.timer);fire(state.fence.timer);
    assert(poll(&state,&token,201)==ESP_ERR_NOT_FINISHED && state.sequence>old.sequence);
    wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,6,&old);
    assert(poll(&state,&token,201)==ESP_ERR_NOT_FINISHED && s_teardown_tx.state.identity==201);
    wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,6,&saved_fence);
    release_error=81;assert(poll(&state,&token,201)==81 && s_teardown_tx.state.identity==201);
    release_error=0;assert(!poll(&state,&token,201) && state.released && !s_teardown_tx.state.identity);
    assert(state.teardown.identity==201 && state.teardown.completion_seen && state.teardown.completion_status==1 && state.teardown.observation_error==83);
    unsigned released=releases;assert(!poll(&state,&token,201) && releases==released && pm_releases==1);
    /* Recycled TX without a definitive callback must remain held. Only the
     * real native connection-close path can revoke its remaining authority. */
    close_seed(202,0);state=(esp32_mquickjs_wifi_btwt_retire_t){0};close_recycle(202);
    s_teardown_tx.state.completion_seen=false;
    assert(poll(&state,&token,202)==ESP_ERR_NOT_FINISHED && !creates && pm_releases==1);
    __wrap_ieee80211_close_all_twt_sessions();close_finish(&state,&token,202);
    assert(!state.teardown.completion_seen && pm_releases==1);
    /* Native submit and TX failures do not trigger another RF attempt, nor
     * become success because the buffer has been returned. */
    close_seed(203,82);state=(esp32_mquickjs_wifi_btwt_retire_t){0};close_recycle(203);
    assert(poll(&state,&token,203)==82 && !creates && s_teardown_tx.state.identity==203);
    assert(poll(&state,&token,203)==82 && !creates);
    __wrap_ieee80211_close_all_twt_sessions();close_finish(&state,&token,203);assert(state.teardown.submit_error==82);
    close_seed(204,0);state=(esp32_mquickjs_wifi_btwt_retire_t){0};close_recycle(204);
    s_teardown_tx.state.completion_status=BTWT_TEARDOWN_FAIL;
    assert(poll(&state,&token,204)==ESP_FAIL && !creates && s_teardown_tx.state.identity==204);
    __wrap_ieee80211_close_all_twt_sessions();close_finish(&state,&token,204);
    /* Close cannot erase a tracking fault or prove unknown recycler state. */
    close_seed(205,0);state=(esp32_mquickjs_wifi_btwt_retire_t){0};close_recycle(205);
    s_teardown_tx.state.fault=84;__wrap_ieee80211_close_all_twt_sessions();
    assert(poll(&state,&token,205)==84 && !creates && esp32_mquickjs_wifi_btwt_setup_held(3));
    memset(&s_teardown_tx,0,sizeof(s_teardown_tx)); /* test-only cold boot */
    reset();assert(!native_live && !locked);
}

int main(void) {
    reset();assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,100,current[3].node,current[3].parameter));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(3,100));
    esp32_mquickjs_wifi_twt_token_t token={123,7},foreign={124,7};
    esp32_mquickjs_wifi_btwt_retire_t state={0};
    cancel_error=71;assert(poll(&state,&token,100)==71 && !creates);
    assert(poll(&state,&foreign,100)==ESP_ERR_INVALID_STATE && cancels==1);
    assert(poll(&state,&token,101)==ESP_ERR_INVALID_STATE && cancels==1);
    cancel_error=0;query_error=ESP_ERR_NOT_FINISHED;
    assert(poll(&state,&token,100)==ESP_ERR_NOT_FINISHED && !creates);
    query_error=0;create_error=72;assert(poll(&state,&token,100)==72 && state.fence.token.identity);
    create_error=0;assert(poll(&state,&token,100)==ESP_ERR_NOT_FINISHED && state.fence.timer && !fence_posts);
    fire(state.fence.timer);block_error=73;
    assert(poll(&state,&token,100)==73 && state.fence.timer && !native_fences && !fence_posts);
    block_error=0;delete_error=74;
    assert(poll(&state,&token,100)==74 && state.fence.stopped && state.fence.timer);
    unsigned stopped=block_stops;delete_error=0;native_error=75;
    assert(poll(&state,&token,100)==75 && !state.fence.timer && block_stops==stopped && !fence_posts);
    native_error=0;fence_error=76;
    assert(poll(&state,&token,100)==76 && !state.sequence && esp32_mquickjs_wifi_btwt_setup_held(3));
    fence_error=0;assert(poll(&state,&token,100)==ESP_ERR_NOT_FINISHED && state.sequence);
    esp32_mquickjs_wifi_btwt_event_fence_t old=saved_fence;
    /* A native connection-close mutation revokes the previous cut. */
    __wrap_ieee80211_close_all_twt_sessions();
    assert(poll(&state,&token,100)==ESP_ERR_NOT_FINISHED && !state.fence.token.identity && !state.sequence);
    assert(poll(&state,&token,100)==ESP_ERR_NOT_FINISHED && state.fence.timer);
    fire(state.fence.timer);assert(poll(&state,&token,100)==ESP_ERR_NOT_FINISHED && state.sequence);
    wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,6,&old);
    esp32_mquickjs_wifi_btwt_timer_result_t result;
    assert(esp32_mquickjs_wifi_btwt_setup_result(3,100,&result) && !result.fence_seen);
    wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,6,&saved_fence);release_error=77;
    assert(poll(&state,&token,100)==77 && !state.released && result.held);
    release_error=0;assert(!poll(&state,&token,100) && state.released && !state.fence.token.identity && !state.stage);
    assert(!esp32_mquickjs_wifi_btwt_setup_held(3));
    unsigned queried=queries,released=releases;
    assert(!poll(&state,&token,100) && queries==queried && releases==released);
    /* Later request cannot consume an old copied event marker. */
    assert(!esp32_mquickjs_wifi_btwt_setup_begin_native(3,101,current[3].node,current[3].parameter));
    assert(!esp32_mquickjs_wifi_btwt_setup_hold_native(3,101));
    wifi_radio_lifecycle_fence(NULL,ESP32QJS_WIFI_RADIO_CONTROL_EVENT,6,&saved_fence);
    state=(esp32_mquickjs_wifi_btwt_retire_t){0};
    assert(poll(&state,&token,101)==ESP_ERR_NOT_FINISHED);fire(state.fence.timer);early_fence=true;
    assert(!poll(&state,&token,101) && state.released && !esp32_mquickjs_wifi_btwt_setup_held(3));
    reset();teardown_retire_cases();assert(!native_live && !locked);return 0;
}
