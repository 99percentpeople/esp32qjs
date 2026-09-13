
int main(void) {
    esp32_mquickjs_event_queue_t queue=1;
    wifi_csi_session_t session={.radio_lease={true},.promiscuous_lease={true},
        .csi_enabled=true,.callback_registered=true,.event_queue=&queue,.event_queue_retained=true};
    atomic_init(&session.lifecycle,WIFI_CSI_RUNNING);session.resources=&session.storage;atomic_init(&session.resources->callbacks_active,0);
    fail_promiscuous=true;
    assert(wifi_csi_begin_stop(&session)==ESP_OK);wifi_csi_finish_stop(&session);wifi_csi_finish_close(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED && session.last_error==-31);
    assert(session.promiscuous_lease.acquired && session.radio_lease.acquired && !released_channel);
    assert(session.event_queue==&queue && session.event_queue_retained && !closed_queue && !released_queue && !destroyed);
    assert(disabled_csi==1 && unregistered_csi==1);
    fail_promiscuous=false;fail_radio=true;
    assert(wifi_csi_begin_stop(&session)==ESP_OK);wifi_csi_finish_stop(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_STOPPED && released_channel==1);
    wifi_csi_finish_close(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED && session.radio_lease.acquired && !closed_queue && !destroyed);
    fail_radio=false;
    assert(wifi_csi_begin_stop(&session)==ESP_OK);wifi_csi_finish_stop(&session);wifi_csi_finish_close(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_CLOSED && !session.radio_lease.acquired && !session.promiscuous_lease.acquired);
    assert(closed_queue==1 && released_queue==1 && destroyed==1 && disabled_csi==1 && unregistered_csi==1);
    wifi_csi_finish_close(&session);assert(closed_queue==1 && destroyed==1);
    /* A failed initial start can still have its initial STOPPED lifecycle but
     * own a partially enabled promiscuous lease: do not bypass cleanup. */
    session=(wifi_csi_session_t){.radio_lease={true},.promiscuous_lease={true}};
    atomic_init(&session.lifecycle,WIFI_CSI_STOPPED);session.resources=&session.storage;atomic_init(&session.resources->callbacks_active,0);
    assert(wifi_csi_begin_stop(&session)==ESP_OK && atomic_load(&session.lifecycle)==WIFI_CSI_STOPPING);
    wifi_csi_finish_stop(&session);assert(!session.promiscuous_lease.acquired);
}
