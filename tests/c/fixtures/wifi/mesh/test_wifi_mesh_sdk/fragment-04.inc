
SETTER(esp_mesh_set_ap_connections,int)
esp_err_t esp_mesh_set_ap_password(const uint8_t*p,int n){assert(p&&n>=8&&n<=64);return step();}
esp_err_t esp_mesh_get_parent_bssid(mesh_addr_t*a){assert(!lock_depth);memset(a,1,sizeof(*a));return 0;}
esp_err_t esp_mesh_get_router_bssid(uint8_t*a){assert(!lock_depth);memset(a,2,6);return 0;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*c,bool block){
 assert(!lock_depth&&block&&c);++scan_starts;retained_scan_config=c;
 assert(c==&s_mesh->scan_config.config);
 if(scan_close_during_start){assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==ESP_ERR_TIMEOUT);assert(!deinits);}
 if(scan_fail)return ESP_ERR_TIMEOUT;
 scan_remaining=2;return 0;
}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t*n){assert(!lock_depth);*n=scan_remaining;return 0;}
esp_err_t esp_mesh_scan_get_ap_ie_len(int*n){assert(!lock_depth&&scan_remaining);*n=scan_ie_length;return 0;}
esp_err_t esp_mesh_scan_get_ap_record(wifi_ap_record_t*a,void*ie){
 assert(!lock_depth&&scan_remaining);++scan_reads;--scan_remaining;memset(a,0,sizeof(*a));
 a->ssid[0]='a';memset(ie,3,scan_ie_length);return 0;
}
esp_err_t esp_mesh_flush_scan_result(void){assert(!lock_depth);if(!scan_ignore_flush)scan_remaining=0;return 0;}
esp_err_t esp_mesh_set_parent(const wifi_config_t*p,const mesh_addr_t*id,mesh_type_t type,int layer){
 assert(!lock_depth&&p);(void)id;(void)type;(void)layer;++parent_sets;return step();
}
