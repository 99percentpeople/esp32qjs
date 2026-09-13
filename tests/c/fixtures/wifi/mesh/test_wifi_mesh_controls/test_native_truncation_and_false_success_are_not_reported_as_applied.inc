
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));unsigned previous=native_calls;
 esp32_mquickjs_wifi_mesh_control_data_t d={.number=255};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_SIGNAL_DUTY,.detail=&d};
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_ARG&&native_calls==previous);
 d.number=254;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 command.kind=ESP32_MQUICKJS_MESH_ASSOC_EXPIRY;d.number=60;ignore_expiry=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_RESPONSE);
 assert(d.completed_steps==1&&!strcmp(d.stage,"mesh-association-expiry-readback"));
 ignore_expiry=false;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 assert(native_expiry==60&&!d.stage);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
