
int main(void){
 test_session=open_session();uint32_t reader=0,other_reader=0;
 assert(!esp32_mquickjs_wifi_mesh_scan_read_claim(test_session,&reader));
 assert(esp32_mquickjs_wifi_mesh_scan_read_claim(test_session,&other_reader)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_scan_read_release(test_session,reader+1);
 assert(test_session->read_identity[2]==reader);
 esp32_mquickjs_wifi_mesh_control_data_t d={.scan_identity=12};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SCAN_NEXT,.detail=&d};
 esp32_mquickjs_wifi_mesh_job_t*j=NULL;
 assert(!esp32_mquickjs_wifi_mesh_job_create(test_session,NULL,&c,1000,&j));
 assert(j->control.detail->scan_record&&j->control.detail->scan_record!=(void*)&d);
 assert((uintptr_t)j->control.detail->scan_record%_Alignof(esp32_mquickjs_wifi_mesh_scan_record_t)==0);
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 assert(esp32_mquickjs_wifi_mesh_scan_commit(test_session,reader,12,1)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_mesh_scan_read_release(test_session,reader);
 esp32_mquickjs_wifi_mesh_session_release(test_session);esp32_mquickjs_wifi_mesh_job_release(j);
 assert(!s_mesh_handles&&!s_mesh_jobs&&!live_allocations);
}
