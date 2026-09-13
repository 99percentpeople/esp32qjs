
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));uint8_t bytes[3]={1,2,3};
 esp32_mquickjs_wifi_mesh_send_t send={.destination=ESP32_MQUICKJS_MESH_TO_ROOT,
  .reliable=true,.protocol=MESH_PROTO_BIN,.bytes=bytes,.length=3};
 other=active;--other.identity;
 assert(esp32_mquickjs_wifi_mesh_sdk_seal(&other)==ESP_ERR_INVALID_STATE&&!s_mesh->status.closing);
 close_during_send=true;assert(!esp32_mquickjs_wifi_mesh_sdk_send(&active,&send));
 assert(s_mesh->status.closing&&!s_mesh->status.busy&&!deinits);
 assert(esp32_mquickjs_wifi_mesh_sdk_inspect(&active)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_mesh_sdk_receive(&active,false)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&deinits==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
