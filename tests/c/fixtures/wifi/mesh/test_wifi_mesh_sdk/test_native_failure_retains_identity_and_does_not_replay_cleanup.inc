
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(2,&c,&active));fail_native=2;
 assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==-77&&inits==1&&!starts);
 assert(s_mesh->status.closing&&!s_mesh->status.restart_required);
 assert(all_zero(&s_mesh->config,sizeof(s_mesh->config)));
 fail_native=0;assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&deinits==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&deinits==1);
 other=active;assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(2,&c,&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_release(&other)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_start(&active)==ESP_ERR_INVALID_STATE&&!s_mesh->status.closing);
 fail_deinit=true;assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==-91);
 unsigned previous=deinits;assert(esp32_mquickjs_wifi_mesh_sdk_close(&active)==-91&&deinits==previous);
 assert(s_mesh->status.restart_required&&s_mesh->status.cleanup_error==-91);
 assert(esp32_mquickjs_wifi_mesh_sdk_release(&active)==ESP_ERR_INVALID_STATE);
}
