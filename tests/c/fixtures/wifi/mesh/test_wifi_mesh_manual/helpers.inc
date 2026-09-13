
static void open_manual(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));native_self_organized=false;
}
static uint32_t begin_scan(void){
 esp32_mquickjs_wifi_mesh_control_data_t d={.data.scan={.config={.scan_type=WIFI_SCAN_TYPE_PASSIVE,
  .scan_time.passive=360,.home_chan_dwell_time=30,.channel_bitmap.ghz_5_channels=1},.has_ssid=true,.ssid={'a','p'}}};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN,.detail=&d};
 int result=esp32_mquickjs_wifi_mesh_sdk_control(&active,&c);
 assert(result==(scan_fail?ESP_ERR_TIMEOUT:0));return s_mesh->status.scan.identity;
}
static void close_manual(void){assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);}
