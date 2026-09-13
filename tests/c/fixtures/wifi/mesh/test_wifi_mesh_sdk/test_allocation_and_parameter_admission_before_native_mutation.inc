
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 c.receive_queue=15;assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active)==ESP_ERR_INVALID_ARG);
 assert(!allocations&&!native_calls);c=config();
 for(unsigned failure=1;failure<=2;++failure){
  allocations=0;fail_allocation=failure;
  assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active)==ESP_ERR_NO_MEM);
  assert(!active.identity&&!s_mesh&&!s_mesh_reserving&&!live_allocations&&!native_calls);
 }
 fail_allocation=0;assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&other)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active)&&!native_calls);
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
 s_mesh_next_identity=UINT32_MAX;assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active));
 assert(active.identity==UINT32_MAX);assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(esp32_mquickjs_wifi_mesh_sdk_reserve(1,&c,&active)==ESP_ERR_NO_MEM);
}
