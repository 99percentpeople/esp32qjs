
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));unsigned previous=native_calls;
 esp32_mquickjs_wifi_mesh_control_data_t d={.data.vote={.percentage=0.9f,.config.attempts=15}};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_WAIVE_ROOT,.detail=&d};
 force_nonroot=true;assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_STATE);
 assert(native_calls==previous&&!d.completed_steps&&!s_mesh->status.busy);
 d.data.vote.is_rc_specified=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_ARG&&native_calls==previous);
 command.kind=ESP32_MQUICKJS_MESH_NETWORK_DUTY;memset(&d,0,sizeof(d));d.number=30;d.duration=-1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_STATE&&native_calls==previous);
 d.duration=1;d.rule=1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_ARG&&native_calls==previous);
 d.rule=0;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)&&native_calls==previous+1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
