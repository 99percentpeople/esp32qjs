
int main(void){
 open_manual();uint32_t scan=begin_scan();esp32_mquickjs_wifi_mesh_scan_record_t a={0};
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan,.scan_record=&a};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT,.detail=&d};
 fail_allocation=allocations+1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_NO_MEM&&!scan_reads&&scan_remaining==2);
 fail_allocation=0;scan_ie_length=ESP32_MQUICKJS_MESH_SCAN_IE_BYTES+1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_SIZE&&!scan_reads&&scan_remaining==2);
 scan_ie_length=4;s_mesh->next_sequence=0;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_NO_MEM&&!scan_reads&&scan_remaining==2);
 close_manual();
}
