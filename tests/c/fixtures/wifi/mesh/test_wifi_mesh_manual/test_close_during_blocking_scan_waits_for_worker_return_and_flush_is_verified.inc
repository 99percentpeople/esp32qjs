
int main(void){
 open_manual();scan_close_during_start=true;begin_scan();assert(s_mesh->status.closing&&!deinits);
 close_manual();scan_close_during_start=false;open_manual();uint32_t scan=begin_scan();
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_FLUSH,.detail=&d};
 scan_ignore_flush=true;assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_RESPONSE);
 assert(s_mesh->status.scan.identity==scan);
 scan_ignore_flush=false;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c));
 assert(!s_mesh->status.scan.identity);close_manual();
}
