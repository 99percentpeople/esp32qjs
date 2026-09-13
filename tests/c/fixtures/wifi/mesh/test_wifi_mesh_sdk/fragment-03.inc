
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool),esp_wifi_scan_get_ap_num(uint16_t*);
esp_err_t esp_mesh_scan_get_ap_ie_len(int*),esp_mesh_scan_get_ap_record(wifi_ap_record_t*,void*),esp_mesh_flush_scan_result(void);
esp_err_t esp_mesh_set_parent(const wifi_config_t*,const mesh_addr_t*,mesh_type_t,int);
esp_err_t esp_mesh_get_parent_bssid(mesh_addr_t*),esp_mesh_get_router_bssid(uint8_t*);
esp_err_t esp_mesh_set_ap_connections(int),esp_mesh_set_ap_password(const uint8_t*,int);
