
static bool owners_allowed(void) {
    wifi_radio_operation_lock();bool allowed=wifi_radio_twt_recovery_owners_locked();wifi_radio_operation_unlock();return allowed;
}
int main(void) {
    individual_reset();s_radio.stop_required=true;
    esp32_mquickjs_wifi_itwt_options_t cfg={.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=6000};
    esp32_mquickjs_wifi_btwt_options_t bc={.config={.setup_cmd=TWT_REQUEST,.btwt_id=1,
        .timeout_time_ms=5000},.timeout_ms=6000};
    esp32_mquickjs_wifi_twt_token_t a={0},b={0},c={0},rejected={0};
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&cfg,&a));
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&cfg,&b));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&bc,&c));
    esp32_mquickjs_wifi_radio_lease_t app={0},foreign={0};
    wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&app));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR,WIFI_MODE_STA,&foreign));
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_recovery_request_t request={s_radio.generation,0,ESP32_MQUICKJS_WIFI_RECOVERY_TWT};
    esp32_mquickjs_wifi_radio_lifecycle_t life={0};wifi_mode_t mode;
    assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_STATE);
    assert(!life.identity && !s_twt_individual[0].closing && !s_twt_broadcast[1]->closing);
    wifi_radio_operation_lock();wifi_radio_release_locked(&foreign);wifi_radio_operation_unlock();
    ++request.generation;assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_STATE);--request.generation;
    request.identity=1;assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_ARG);request.identity=0;
    eap_pending=true;assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_STATE);eap_pending=false;
    assert(!wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode));
    assert(mode==WIFI_MODE_STA && life.identity && individual_owners()==3);
    assert(s_twt_individual[0].closing && s_twt_individual[1].closing && s_twt_broadcast[1]->closing);
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&cfg,&rejected)==ESP_ERR_INVALID_STATE && !rejected.identity);
    assert(!owners_allowed()); /* helper has not transferred yet */
    wifi_radio_operation_lock();wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    assert(owners_allowed());
    esp32_mquickjs_wifi_radio_lifecycle_t stale=life;++stale.identity;
    assert(wifi_radio_twt_recovery_prepare(&stale)==ESP_ERR_INVALID_STATE);
    assert(!wifi_radio_twt_recovery_prepare(&life) && !cancelled);
    assert(!wifi_radio_twt_recovery_checkpoint(&life,mode) && prepared==1 && captured==1);
    assert(wifi_radio_twt_recovery_phase(&stale,false)==ESP_ERR_INVALID_STATE && !stop_calls);
    physical_error=77;assert(wifi_radio_twt_recovery_phase(&life,false)==77 && stop_calls==1);
    physical_error=0;assert(!wifi_radio_twt_recovery_phase(&life,false));
    /* SDK STOP observation boundary, without fabricating owner retirement. */
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;s_twt_recovery.stopped=true;
    assert(!wifi_radio_twt_recovery_stopped(&life));
    assert(wifi_radio_twt_recovery_finish(&life)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&a));
    assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&b));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&c));
    assert(!individual_owners() && !wifi_radio_twt_recovery_phase(&life,true) && shutdown_calls==1);
    s_radio.driver_owned=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;++s_radio.generation;
    assert(!wifi_radio_twt_recovery_finish(&life) && !s_twt_recovery.lifecycle.identity);
    assert(s_radio.lifecycle.identity==life.identity); /* replay still owns lifecycle */
    /* Live probe cancellation is a required, retryable pre-disconnect prefix. */
    memset(&s_radio.lifecycle,0,sizeof(s_radio.lifecycle));s_radio.driver_owned=s_radio.started=s_radio.stop_required=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    esp32_mquickjs_wifi_twt_token_t probe={0};assert(!esp32_mquickjs_wifi_radio_twt_probe_submit(100,&probe));
    request.generation=s_radio.generation;life=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
    assert(!wifi_radio_twt_recovery_begin(NULL,NULL,NULL,&request,&life,&mode));
    assert(s_twt_probe.state.cleanup_pending && individual_owners()==1);
    cancel_error=78;assert(wifi_radio_twt_recovery_prepare(&life)==78 && !s_twt_recovery.prepared && cancelled==1);
    cancel_error=0;assert(!wifi_radio_twt_recovery_prepare(&life) && s_twt_recovery.prepared && cancelled==2);
    assert(!wifi_radio_twt_recovery_prepare(&life) && cancelled==2);
    assert(!esp32_mquickjs_wifi_radio_twt_probe_retire(&probe));
    assert(!individual_owners() && !critical && !locks);
    free(s_twt_individual);free(s_twt_broadcast);return 0;
}
