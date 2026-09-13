
int main(void){
 s_wifi_state.status.connected=true;s_wifi_state.connect_generation=33;
 uint32_t id=0;esp32_mquickjs_wifi_radio_lease_t owners[3];
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==0 && id==1);
 assert(owners[0].identity==1 && owners[1].identity==2 && owners[2].identity==3);
 assert(s_wifi_state.status.connected && s_wifi_state.connect_generation==33);
 assert(esp32_mquickjs_wifi_wps_ap_helper_held() && !wifi_helpers_idle(true));
 assert(esp32_mquickjs_wifi_connection_reserved_locked());
 uint32_t wrong=id+1;assert(esp32_mquickjs_wifi_wps_ap_helper_release(&wrong)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_wps_ap_helper_held());
 uint32_t old=id;assert(esp32_mquickjs_wifi_wps_ap_helper_release(&id)==0 && !id && wifi_helpers_idle(true));
 assert(s_wifi_state.status.connected && s_wifi_state.connect_generation==33);
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==0 && id>old);
 assert(esp32_mquickjs_wifi_wps_ap_helper_release(&old)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_wps_ap_helper_release(&id)==0);
 s_wifi_wps_capture_identity=7;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_INVALID_STATE && !id);
 s_wifi_wps_capture_identity=0;ap.acquired=false;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_INVALID_STATE);
 ap.acquired=true;s_wifi_state.connect_draining=true;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_INVALID_STATE);
 s_wifi_state.connect_draining=false;s_wifi_wps_ap_next_helper_identity=0;
 assert(esp32_mquickjs_wifi_wps_ap_helper_reserve(&id,owners)==ESP_ERR_NO_MEM && !id);
 return 0;
}
