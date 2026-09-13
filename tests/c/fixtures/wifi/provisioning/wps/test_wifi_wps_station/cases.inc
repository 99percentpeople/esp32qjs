
static void event(int kind,unsigned generation) {
    esp32_mquickjs_wifi_driver_event_t e={.kind=kind,.generation=generation};wifi_process_driver_event(&e);
}
int main(void) {
    s_wifi_state.sta_netif=(void*)1;
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){.identity=2,.generation=5,.acquired=true};
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){.identity=1,.generation=5,.acquired=true};
    uint32_t identity=0;esp32_mquickjs_wifi_radio_lease_t owners[3];
    assert(!esp32_mquickjs_wifi_wps_station_reserve(&identity,owners) && identity==1);
    assert(owners[0].identity==1 && owners[1].identity==2 && !owners[2].acquired);
    assert(s_wifi_state.connect_generation==1 && !s_wifi_state.connect_in_progress);
    assert(!wifi_helpers_idle(true));
    esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
    esp32_mquickjs_future_driver_state_t connect={.kind=WIFI_FUTURE_CONNECT};
    esp32_mquickjs_future_driver_state_t disconnect={.kind=WIFI_FUTURE_DISCONNECT};
    assert(!wifi_future_start(NULL,NULL,1,&scan));
    assert(!wifi_future_start(NULL,NULL,2,&connect));
    assert(!wifi_future_start(NULL,NULL,3,&disconnect));
    assert(esp32_mquickjs_wifi_cancel_connect(1)==ESP_ERR_INVALID_STATE && !disconnect_calls);
    event(WIFI_DRIVER_EVENT_GOT_IP,0);assert(!s_wifi_state.status.connected && !queued && !wakes);
    uint32_t wrong=identity+1;
    assert(esp32_mquickjs_wifi_wps_station_release(&wrong)==ESP_ERR_INVALID_STATE && s_wifi_wps_capture_identity==identity);
    netif_up=true;assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_ERR_NOT_FINISHED);
    netif_up=false;post_error=ESP_FAIL;event(WIFI_DRIVER_EVENT_DISCONNECTED,0);
    assert(s_wifi_state.connect_draining && !s_wifi_state.disconnect_fence_posted);
    assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_FAIL && !disconnect_calls);
    post_error=0;assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_ERR_NOT_FINISHED);
    event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch+1);
    assert(esp32_mquickjs_wifi_wps_station_drain(identity)==ESP_ERR_NOT_FINISHED);
    event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch);
    assert(!esp32_mquickjs_wifi_wps_station_drain(identity) && s_wifi_wps_capture_identity==identity);
    uint32_t old=identity;assert(!esp32_mquickjs_wifi_wps_station_release(&identity) && !identity && wifi_helpers_idle(false));
    assert(!esp32_mquickjs_wifi_wps_station_reserve(&identity,owners) && identity!=old && s_wifi_state.connect_generation==2);
    assert(esp32_mquickjs_wifi_wps_station_release(&old)==ESP_ERR_INVALID_STATE && s_wifi_wps_capture_identity==identity);
    assert(!esp32_mquickjs_wifi_wps_station_release(&identity));
    /* DPP shares the slot but a WPS release cannot free its current owner. */
    assert(!esp32_mquickjs_wifi_dpp_station_reserve(&identity,owners));
    old=identity;
    assert(esp32_mquickjs_wifi_wps_station_release(&old)==ESP_ERR_INVALID_STATE);
    assert(s_wifi_wps_capture_identity==identity && !wifi_helpers_idle(false));
    assert(!esp32_mquickjs_wifi_dpp_station_release(&identity));
    assert(esp32_mquickjs_wifi_dpp_station_release(&old)==ESP_ERR_INVALID_STATE);
    s_wifi_wps_next_identity=0;
    assert(esp32_mquickjs_wifi_wps_station_reserve(&identity,owners)==ESP_ERR_NO_MEM && !identity);
    s_wifi_wps_next_identity=3;s_wifi_state.connect_generation=UINT32_MAX;
    assert(esp32_mquickjs_wifi_wps_station_reserve(&identity,owners)==ESP_ERR_NO_MEM && !identity);
    assert(!disconnect_calls);
    return 0;
}
