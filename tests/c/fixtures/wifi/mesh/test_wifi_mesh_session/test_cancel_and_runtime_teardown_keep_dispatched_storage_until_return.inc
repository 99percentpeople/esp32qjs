
int main(void){
 test_session=open_session();test_job=new_job(test_session,1000);
 assert(!esp32_mquickjs_wifi_mesh_job_submit(test_job));cancel_during_send=true;tick();
 assert(radio_sends==1&&radio_closes==1&&!test_job&&!s_mesh_jobs);
 assert(test_session->status.retired&&esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
