
static const esp32_mquickjs_wifi_radio_operation_t owner={.identity=19,.generation=5,.lease_identity=2,.kind=3};
static const wifi_config_t config={.sta.ssid="fixture",.sta.pmf_cfg.capable=true};
static void event(int kind,unsigned generation){
 esp32_mquickjs_wifi_driver_event_t e={.kind=kind,.generation=generation};wifi_process_driver_event(&e);
}
static void reset(void){
 assert(!borrowed.identity && !s_wifi_smartconfig_connect.owner.identity && !lock_depth);
 memset(&s_wifi_state,0,sizeof(s_wifi_state));s_wifi_state.sta_netif=(void *)1;
 borrow_error=submit_error=release_error=post_error=disconnect_error=0;
 queued=wakes=disconnect_calls=posts=borrow_calls=release_calls=submit_calls=0;
}
int main(void){
 reset();uint32_t generation=0;
 assert(!esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation));
 assert(generation==1 && !s_wifi_state.connect_future_registered && borrowed.identity);
 esp32_mquickjs_wifi_smartconfig_connection_status_t status;
 esp32_mquickjs_future_driver_state_t scan={.kind=WIFI_FUTURE_SCAN};
 assert(!wifi_future_start(NULL,NULL,1,&scan));
 assert(esp32_mquickjs_wifi_cancel_connect(generation)==ESP_ERR_INVALID_STATE && !disconnect_calls);
 wifi_queue_connect_event(generation+1,2,77,NULL);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && !status.terminal);
 event(WIFI_DRIVER_EVENT_GOT_IP,0);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && status.terminal==1 && status.connected);
 assert(!queued && !wakes); /* A native result exists without a queue or Future. */
 wifi_queue_connect_event(generation,2,88,NULL);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && status.terminal==1);
 esp32_mquickjs_wifi_radio_operation_t stale=owner;stale.lease_identity++;
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&stale,generation,false)==ESP_ERR_INVALID_STATE && !disconnect_calls);
 release_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,true)==ESP_FAIL && borrowed.identity);
 release_error=0;assert(!esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,true));
 assert(s_wifi_state.status.connected && !disconnect_calls && !borrowed.identity);

 reset();generation=0;assert(!esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation));
 event(WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,generation+1);assert(!disconnect_calls);
 event(WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,generation);assert(disconnect_calls==1);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_status(&owner,generation,&status) && status.terminal==3);
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false)==ESP_ERR_NOT_FINISHED && !release_calls);
 post_error=ESP_FAIL;event(WIFI_DRIVER_EVENT_DISCONNECTED,0);
 assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false)==ESP_FAIL && !release_calls);
 post_error=0;assert(esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false)==ESP_ERR_NOT_FINISHED);
 event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch+1);assert(s_wifi_state.connect_draining);
 event(WIFI_DRIVER_EVENT_LINK_DRAINED,fence_epoch);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false) && release_calls==1);
 assert(disconnect_calls==1 && !s_wifi_smartconfig_connect.owner.identity);

 reset();generation=0;submit_error=ESP_FAIL;
 assert(esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation)==ESP_FAIL && generation && borrowed.identity);
 assert(!esp32_mquickjs_wifi_smartconfig_connect_end(&owner,generation,false) && !disconnect_calls);
 reset();generation=0;s_wifi_state.connect_generation=UINT32_MAX;
 assert(esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation)==ESP_ERR_NO_MEM && !borrow_calls && !generation);
 reset();generation=0;s_wifi_state.connect_draining=true;
 assert(esp32_mquickjs_wifi_smartconfig_connect_begin(&owner,&config,1000,&generation)==ESP_ERR_NOT_FINISHED && !borrow_calls);
 return 0;
}
