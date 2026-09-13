
int main(void) {
    individual_reset();
    esp32_mquickjs_wifi_btwt_options_t options={.config={.setup_cmd=TWT_REQUEST,.btwt_id=1,.timeout_time_ms=5000},.timeout_ms=6000};
    esp32_mquickjs_wifi_twt_token_t a={0},b={0},stale;
    allocation_fails=true;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a)==ESP_ERR_NO_MEM);
    assert(!a.identity && !broadcast_submits && !individual_owners());allocation_fails=false;
    broadcast_early=true;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a));
    assert(a.identity && broadcast_submits==1 && individual_owners()==1);
    /* Regression: duplicate ID must not return ESP_OK with a zero token. */
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&b)==ESP_ERR_INVALID_STATE);
    assert(!b.identity && broadcast_submits==1 && individual_owners()==1);
    esp32_mquickjs_wifi_radio_lease_t borrowed=s_twt_broadcast[1]->lease;
    wifi_radio_operation_lock();wifi_radio_release_locked(&borrowed);wifi_radio_operation_unlock();assert(borrowed.acquired);
    options.config.btwt_id=2;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&b));
    stale=a;++stale.generation;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_request_close(&stale));
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&stale)==ESP_ERR_INVALID_STATE);
    native_individual.broadcast_id_bitmap=1U<<1;
    broadcast_retire_error=ESP_ERR_NOT_FINISHED;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&a)==ESP_ERR_NOT_FINISHED && broadcast_teardowns==1);
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&a)==ESP_ERR_NOT_FINISHED && broadcast_teardowns==1);
    esp32_mquickjs_wifi_twt_broadcast_radio_state_t status;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_status(&a,&status));
    assert(status.closing && status.dialog_attempts_remaining==254 && status.teardown_attempted);
    stale=a;broadcast_retire_error=0;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a) && !a.identity && individual_owners()==1);
    options.config.btwt_id=1;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a) && a.identity!=stale.identity);
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_request_close(&stale));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_status(&stale,&status));
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&stale)==ESP_ERR_INVALID_STATE);
    assert(individual_owners()==2);
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&b));
    /* Exhaustion is per physical-boot ID and must precede any new lease/SDK call. */
    broadcast_attempts[1]=255;unsigned submits_before=broadcast_submits;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a)==ESP_ERR_NO_MEM);
    assert(!a.identity && !individual_owners() && broadcast_submits==submits_before);
    options.config.btwt_id=2;broadcast_accept=false;broadcast_submit_error=77;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a)==77 && a.identity);
    unsigned retires_before=broadcast_retires;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a) && broadcast_retires==retires_before);
    /* Unconfirmed handoff cannot release another owner's channel/Radio lease. */
    broadcast_accept=true;broadcast_submit_error=0;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&options,&a));
    s_twt_broadcast[2]->dispatch.handoff_error=78;
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_close(&a)==78 && individual_owners()==1);
    assert(esp32_mquickjs_wifi_radio_twt_broadcast_status(&a,&status) && status.cleanup_error==78);
    s_twt_broadcast[2]->dispatch.handoff_error=0;
    broadcast_results[2].native_closed=true;
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&a));
    assert(!locks && !critical && !individual_owners());free(s_twt_broadcast);return 0;
}
