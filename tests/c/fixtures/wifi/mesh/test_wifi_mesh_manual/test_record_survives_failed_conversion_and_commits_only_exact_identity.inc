
int main(void){
 open_manual();uint32_t scan=begin_scan();
 esp32_mquickjs_wifi_mesh_scan_record_t a={0},b={0};
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=scan,.scan_record=&a};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT,.detail=&d};
 assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)&&scan_reads==1&&a.sequence);
 assert(s_mesh->status.scan.remaining==2&&s_mesh->status.scan.retained);
 /* First caller failed conversion: no commit. Next caller gets the same row. */
 d.scan_record=&b;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c));
 assert(!memcmp(&a,&b,sizeof(a))&&scan_reads==1);
 assert(esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan+1,a.sequence)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan,a.sequence+1)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan,a.sequence));
 assert(s_mesh->status.scan.remaining==1&&!s_mesh->status.scan.retained);
 assert(esp32_mquickjs_wifi_mesh_sdk_scan_commit(&active,scan,a.sequence)==ESP_ERR_INVALID_STATE);
 c.kind=ESP32_MQUICKJS_MESH_SCAN_FLUSH;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c));
 assert(!s_mesh->scan_record&&live_allocations==2);
 uint32_t next=begin_scan();assert(next!=scan);
 c.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE&&scan_reads==1);
 close_manual();
}
