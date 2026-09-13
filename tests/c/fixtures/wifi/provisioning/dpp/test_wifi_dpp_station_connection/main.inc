
static const esp32_mquickjs_wifi_radio_operation_t owner={.identity=19,.generation=5,.lease_identity=2,.kind=ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP};
static const wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};
static void event(int kind,unsigned generation){
 esp32_mquickjs_wifi_driver_event_t e={.kind=kind,.generation=generation};wifi_process_driver_event(&e);
}
static void reset(void){
 assert(!borrowed.identity&&!s_wifi_smartconfig_connect.owner.identity&&!lock_depth);
 memset(&s_wifi_state,0,sizeof(s_wifi_state));s_wifi_state.sta_netif=(void*)1;
 s_wifi_wps_capture_identity=7;s_wifi_station_capture_dpp=true;
 borrow_error=submit_error=release_error=post_error=disconnect_error=timer_error=0;
 queued=wakes=disconnect_calls=posts=borrow_calls=release_calls=submit_calls=0;
}
int main(void){
 reset();uint32_t generation=0;
 assert(esp32_mquickjs_wifi_dpp_connect_begin(8,&owner,&config,1000,&generation)==ESP_ERR_INVALID_STATE&&!borrow_calls);
 s_wifi_station_capture_dpp=false;
 assert(esp32_mquickjs_wifi_dpp_connect_begin(7,&owner,&config,1000,&generation)==ESP_ERR_INVALID_STATE&&!borrow_calls);
 s_wifi_station_capture_dpp=true;timer_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_dpp_connect_begin(7,&owner,&config,1000,&generation)==ESP_FAIL&&!borrow_calls&&!generation);
 timer_error=0;
 assert(!esp32_mquickjs_wifi_dpp_connect_begin(7,&owner,&config,1000,&generation));
 assert(generation==1&&s_wifi_wps_capture_identity==7&&s_wifi_smartconfig_connect.owner.kind==owner.kind);
 assert(!s_wifi_state.connect_future_registered&&borrowed.identity);
 esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
 assert(!wifi_future_start(NULL,NULL,1,&scan));
 assert(esp32_mquickjs_wifi_cancel_connect(generation)==ESP_ERR_INVALID_STATE&&!disconnect_calls);
 esp32_mquickjs_wifi_dpp_connection_status_t status;
 wifi_queue_connect_event(generation+1,2,77,NULL);
 assert(!esp32_mquickjs_wifi_dpp_connect_status(&owner,generation,&status)&&!status.terminal);
 s_wifi_state.status.link=(esp32_mquickjs_wifi_link_snapshot_t){.valid=true,.bssid={2,3,4,5,6,7},.channel=6,.aid=9};
 event(WIFI_DRIVER_EVENT_GOT_IP,0);
 assert(!esp32_mquickjs_wifi_dpp_connect_status(&owner,generation,&status)&&status.terminal==1&&status.connected);
 assert(status.link.valid&&status.link.bssid[5]==7&&status.link.channel==6&&status.link.aid==9);
 assert(!queued&&!wakes); /* Terminal is independent of a JS Future/queue. */
 wifi_queue_connect_event(generation,2,88,NULL);
 assert(!esp32_mquickjs_wifi_dpp_connect_status(&owner,generation,&status)&&status.terminal==1);
 esp32_mquickjs_wifi_radio_operation_t stale=owner;stale.lease_identity++;
 assert(esp32_mquickjs_wifi_dpp_connect_end(&stale,generation)==ESP_ERR_INVALID_STATE&&!disconnect_calls);
 assert(esp32_mquickjs_wifi_dpp_connect_end(&owner,generation+1)==ESP_ERR_INVALID_STATE&&!disconnect_calls);
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,true)==ESP_ERR_INVALID_STATE&&!release_calls);
 assert(esp32_mquickjs_wifi_dpp_connect_end(&owner,generation)==ESP_ERR_NOT_FINISHED&&disconnect_calls==1&&!release_calls);
 post_error=ESP_FAIL;event(WIFI_DRIVER_EVENT_DISCONNECTED,0);
 assert(esp32_mquickjs_wifi_dpp_connect_end(&owner,generation)==ESP_FAIL&&!release_calls);
 s_wifi_state.status.last_disconnect_reason=55;
 assert(!esp32_mquickjs_wifi_dpp_connect_status(&owner,generation,&status)&&!status.connected&&status.reason==55&&!status.link.valid);
 post_error=0;assert(esp32_mquickjs_wifi_dpp_connect_end(&owner,generation)==ESP_ERR_NOT_FINISHED);
 event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch+1);assert(s_wifi_state.connect_draining);
 event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch);timer_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_dpp_connect_end(&owner,generation)==ESP_FAIL&&!release_calls);
 timer_error=0;release_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_dpp_connect_end(&owner,generation)==ESP_FAIL&&borrowed.identity);
 release_error=0;assert(!esp32_mquickjs_wifi_dpp_connect_end(&owner,generation));
 assert(disconnect_calls==1&&!borrowed.identity&&!s_wifi_smartconfig_connect.owner.identity&&s_wifi_wps_capture_identity==7);
 assert(esp32_mquickjs_wifi_dpp_connect_end(&owner,generation)==ESP_ERR_INVALID_STATE);

 reset();generation=0;submit_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_dpp_connect_begin(7,&owner,&config,1000,&generation)==ESP_FAIL&&generation&&borrowed.identity);
 assert(!esp32_mquickjs_wifi_dpp_connect_end(&owner,generation)&&!disconnect_calls);
 reset();generation=0;s_wifi_state.connect_generation=UINT32_MAX;
 assert(esp32_mquickjs_wifi_dpp_connect_begin(7,&owner,&config,1000,&generation)==ESP_ERR_NO_MEM&&!borrow_calls&&!generation);
 reset();s_wifi_state.connect_draining=true;
 assert(esp32_mquickjs_wifi_dpp_connect_begin(7,&owner,&config,1000,&generation)==ESP_ERR_INVALID_STATE&&!borrow_calls);
 return 0;
}
