
int main(void){
 test_session=open_session();uint32_t a=0,b=0,c=0;
 assert(!esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&a));
 assert(!esp32_mquickjs_wifi_mesh_read_claim(test_session,true,&b));
 assert(a!=b&&esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&c)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_read_release(test_session,false,b);
 assert(test_session->read_identity[0]==a);
 esp32_mquickjs_wifi_mesh_read_release(test_session,false,a);
 s_mesh_job_next=UINT32_MAX;assert(!esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&c));
 assert(c==UINT32_MAX);esp32_mquickjs_wifi_mesh_read_release(test_session,false,c);c=0;
 assert(esp32_mquickjs_wifi_mesh_read_claim(test_session,false,&c)==ESP_ERR_NO_MEM);
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
