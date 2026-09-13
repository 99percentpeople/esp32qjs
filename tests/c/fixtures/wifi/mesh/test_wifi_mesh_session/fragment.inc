
esp_err_t esp32_mquickjs_wifi_radio_mesh_scan_commit(const esp32_mquickjs_wifi_mesh_radio_token_t*t,uint32_t id,uint32_t sequence){
 assert(!lock_depth&&t->identity&&id&&sequence);return 0;
}
