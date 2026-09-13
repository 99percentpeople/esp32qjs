
int main(void){
 open_manual();scan_fail=true;uint32_t scan=begin_scan();
 assert(s_mesh->status.scan.uncertain&&!s_mesh->status.scan.completed);
 assert(retained_scan_config->ssid==s_mesh->scan_config.ssid&&!memcmp(retained_scan_config->ssid,"ap",3));
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_FLUSH,.detail=&d};
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE);
 c.kind=ESP32_MQUICKJS_MESH_SELF_ORGANIZED;d.enabled=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE);
 assert(scan_starts==1&&!native_self_organized);
 close_manual();
}
