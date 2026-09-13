
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 for(unsigned i=0;i<ESP32_MQUICKJS_MESH_EVENT_SLOTS;++i)
  assert(__wrap_esp_mesh_send_event_internal(MESH_EVENT_STARTED,NULL,0)==ESP_ERR_TIMEOUT);
 mesh_event_connected_t joined={.self_layer=3,.connected={.channel=6,.bssid={2,3,4,5,6,7}}};
 esp32_mquickjs_wifi_mesh_sdk_capture(MESH_EVENT_PARENT_CONNECTED,&joined,sizeof(joined));
 assert(s_mesh->status.parent_connected&&s_mesh->status.layer==3&&s_mesh->status.channel==6);
 mesh_event_disconnected_t left={.reason=17};
 esp32_mquickjs_wifi_mesh_sdk_capture(MESH_EVENT_PARENT_DISCONNECTED,&left,sizeof(left));
 assert(!s_mesh->status.parent_connected&&s_mesh->status.dropped_events==2);
 esp32_mquickjs_wifi_mesh_notice_t a,b;
 assert(!esp32_mquickjs_wifi_mesh_sdk_event_copy(&active,&a));
 assert(!esp32_mquickjs_wifi_mesh_sdk_event_copy(&active,&b)&&a.sequence==b.sequence);
 assert(esp32_mquickjs_wifi_mesh_sdk_event_commit(&active,a.sequence+1)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_event_commit(&active,a.sequence));
 assert(esp32_mquickjs_wifi_mesh_sdk_event_commit(&active,a.sequence)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_sdk_capture(MESH_EVENT_PARENT_CONNECTED,NULL,sizeof(joined));
 assert(!s_mesh->status.parent_known&&s_mesh->status.malformed_events==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(!live_allocations&&!lock_depth);
}
