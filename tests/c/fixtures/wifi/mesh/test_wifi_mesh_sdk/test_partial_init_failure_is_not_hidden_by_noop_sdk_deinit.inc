
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(3,&c,&active));
 fail_native=1;assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==-77);
 assert(s_mesh->status.restart_required&&!s_mesh->status.initialized);
 assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==ESP_ERR_INVALID_STATE&&!deinits);
 assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==ESP_ERR_INVALID_STATE&&inits==1);
 assert(all_zero(&s_mesh->config,sizeof(s_mesh->config)));
}
