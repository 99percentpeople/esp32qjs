
int main(void){
 assert(!esp32_mquickjs_wifi_mesh_open_runtime());
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_session_create(&c,false,1000,&test_session));
 assert(!esp32_mquickjs_wifi_mesh_session_activate(test_session));
 assert(esp32_mquickjs_wifi_mesh_service()&&pending_worker);
 assert(!esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());run_worker();
 assert(!radio_begins&&!radio_closes&&test_session->status.retired);
 assert(all_zero(&test_session->config,sizeof(test_session->config)));
 assert(esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
