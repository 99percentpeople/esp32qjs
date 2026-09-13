
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();memcpy(c.network.router.password,"password",8);
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_inspect(&active)&&!layer_reads);
 esp32_mquickjs_wifi_mesh_control_data_t d={0};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_CONFIGURATION,.detail=&d};
 assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 assert(!d.data.configuration.config.network.crypto_funcs);
 assert(all_zero(d.data.configuration.config.network.router.password,64));
 assert(all_zero(d.data.configuration.config.ie_key,64));
 assert(d.data.configuration.config.ie_key_length==8);
 d.include_secrets=true;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 assert(!memcmp(d.data.configuration.config.network.router.password,"password",8));
 assert(!memcmp(d.data.configuration.config.ie_key,"abcdefgh",8));
 fail_native=native_calls+2;assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==-77);
 assert(all_zero(d.data.configuration.config.network.router.password,64));
 assert(all_zero(d.data.configuration.config.ie_key,64));
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
