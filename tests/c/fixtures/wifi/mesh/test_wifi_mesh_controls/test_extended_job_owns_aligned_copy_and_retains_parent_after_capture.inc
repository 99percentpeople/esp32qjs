
int main(void){
 test_session=open_session();
 esp32_mquickjs_wifi_mesh_control_data_t d={.data.router={.ssid_len=2,.ssid={'a','p'},.password={'s','e','c','r','e','t','1','2'}}};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_SET_ROUTER,.detail=&d};
 esp32_mquickjs_wifi_mesh_job_t*j=NULL;
 assert(!esp32_mquickjs_wifi_mesh_job_create(test_session,NULL,&command,1000,&j));
 memset(&d,0,sizeof(d));
 assert((uintptr_t)j->control.detail%_Alignof(esp32_mquickjs_wifi_mesh_control_data_t)==0);
 assert(!memcmp(j->control.detail->data.router.password,"secret12",8));
 assert(!esp32_mquickjs_wifi_mesh_job_detail(j));
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 assert(test_session->status.retired);
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(s_mesh_handles==1);
 esp32_mquickjs_wifi_mesh_job_release(j);assert(!s_mesh_handles&&!s_mesh_jobs&&!live_allocations);
}
