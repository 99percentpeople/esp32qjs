
static wifi_action_tx_req_t *request;
static size_t request_bytes;
static esp32_mquickjs_wifi_action_token_t operation;
static esp32_mquickjs_wifi_radio_lifecycle_t recovery;
static void setup_recovery(void) {
    reset_action();memset(&operation,0,sizeof(operation));memset(&recovery,0,sizeof(recovery));
    request->op_id=0; /* Each independent SDK submission starts unassigned. */
    memset(&s_config_restart,0,sizeof(s_config_restart));memset(&s_policy_restart,0,sizeof(s_policy_restart));
    s_radio.stop_required=true;s_radio.event_live=WIFI_MODE_STA;s_channel_event_instance=&s_channel_callbacks;
    atomic_store(&s_channel_callbacks,0);stop_calls=stop_waits=deinit_calls=unregister_calls=0;
    stop_error=wait_error=deinit_error=unregister_error=0;wait_expired=false;
    assert(esp32_mquickjs_wifi_radio_action_send(request,request_bytes,&operation)==ESP_OK);
}
static int admit(const esp32_mquickjs_wifi_radio_lease_t *application) {
    wifi_mode_t mode=(wifi_mode_t)99;
    int error=esp32_mquickjs_wifi_radio_begin_action_recovery(application,NULL,NULL,&operation,&recovery,&mode);
    assert(mode==(error==ESP_OK?WIFI_MODE_STA:WIFI_MODE_NULL));
    assert(!stop_calls && !deinit_calls && !unregister_calls && action_owners()==1);
    return error;
}
static void late_owner(void) {s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=500;}
int main(void) {
    request_bytes=sizeof(*request)+1;request=calloc(1,request_bytes);assert(request);
    *request=(wifi_action_tx_req_t){.ifx=WIFI_IF_STA,.type=WIFI_OFFCHAN_TX_REQ,.channel=6,
        .wait_time_ms=100,.rx_cb=esp32_mquickjs_wifi_action_receive,.data_len=1};
    for(unsigned slot=1;slot<WIFI_RADIO_MAX_LEASES;++slot) {
        setup_recovery();s_radio.leases[slot].identity=500;
        assert(admit(NULL)==ESP_ERR_INVALID_STATE && !recovery.identity && operation.identity);
    }
    setup_recovery();on_lock=late_owner;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();++operation.identity;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();++operation.generation;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_radio.wake_locks=1;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_interval.restore_pending=true;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_tx_rate_lease.restore_pending=true;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_radio.next_lifecycle_identity=0;assert(admit(NULL)==ESP_ERR_NO_MEM);
    setup_recovery();s_radio.next_lifecycle_identity=UINT32_MAX;assert(admit(NULL)==ESP_OK);
    assert(recovery.identity==UINT32_MAX && !s_radio.next_lifecycle_identity);

    setup_recovery();esp32_mquickjs_wifi_radio_lease_t application={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&application)==0);
    wifi_radio_operation_unlock();
    assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    assert(admit(&application)==ESP_OK && application.acquired && operation.identity);
    wifi_radio_operation_lock();
    assert(wifi_radio_action_recovery_capture_owner_locked(&recovery)==s_action.lease.identity);
    s_action.lane.cancel_busy=true;assert(!wifi_radio_action_recovery_capture_owner_locked(&recovery));
    s_action.lane.cancel_busy=false;
    wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==ESP_ERR_INVALID_STATE && !stop_calls);
    wifi_radio_operation_lock();wifi_radio_release_locked(&application);wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_radio_lifecycle_t stale=recovery;++stale.identity;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&stale)==ESP_ERR_INVALID_STATE && !stop_calls);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_action_recovery_active(&recovery));
    assert(!esp32_mquickjs_wifi_radio_action_recovery_active(&stale));
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    stop_error=71;assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==71 && stop_calls==1);
    stop_error=0;wait_error=72;
    assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==72 && stop_calls==2 && s_radio.stop_submitted);
    wait_error=0;assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==0 && stop_calls==2);
    assert(!s_action.lane.physical_termination && !s_radio.started && operation.identity);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&stale)==ESP_ERR_INVALID_STATE);
    s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=500;
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=0;
    s_action.lane.cancel_busy=true;
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    s_action.lane.cancel_busy=false;
    unregister_error=73;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==73 && !deinit_calls);
    unregister_error=0;wait_expired=true;atomic_store(&s_channel_callbacks,1);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==ESP_ERR_TIMEOUT && !deinit_calls);
    assert(!s_channel_event_instance && unregister_calls==2 && !s_action.lane.physical_termination);
    atomic_store(&s_channel_callbacks,0);wait_expired=false;deinit_error=74;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==74 && deinit_calls==1);
    assert(s_radio.driver_owned && !s_action.lane.physical_termination && s_radio.generation==7);
    deinit_error=0;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==ESP_ERR_TIMEOUT && deinit_calls==2);
    assert(!s_radio.driver_owned && s_action.lane.physical_termination && !s_action.lane.terminal);
    assert(operation.identity && action_owners()==1 && s_radio.generation==7);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==ESP_ERR_TIMEOUT && deinit_calls==2);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_OK);
    unsigned queries=quiescent_calls,cancels=cancel_calls,fences=fence_calls,markers=posts;
    assert(esp32_mquickjs_wifi_radio_action_cancel(&operation)==0 && cancel_calls==cancels);
    esp32_mquickjs_wifi_action_lane_t result;
    assert(esp32_mquickjs_wifi_radio_action_retire(&operation,&result)==ESP_OK);
    assert(result.physical_termination && !result.terminal && result.terminal_status==-1);
    assert(!operation.identity && !action_owners() && s_radio.generation==7);
    assert(quiescent_calls==queries && fence_calls==fences && posts==markers);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==0);
    assert(s_radio.generation==8 && deinit_calls==2 && stop_calls==2 && unregister_calls==2);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==0 && s_radio.generation==8);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_OK);
    assert(!esp32_mquickjs_wifi_radio_action_recovery_active(&recovery));
    assert(s_radio.lifecycle.identity==recovery.identity && s_radio.generation==8);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    stale=recovery;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&recovery,true)==0 && !recovery.identity);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_radio_action_recovery_active(&stale));
    assert(!s_radio.lifecycle.identity && !locks && !critical);
    free(request);return 0;
}
