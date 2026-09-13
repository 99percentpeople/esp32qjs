
int main(void){
 test_session=open_session();test_job=new_job(test_session,100);
 assert(!esp32_mquickjs_wifi_mesh_job_submit(test_job));
 radio_send_duration=200000;radio_send_error=-77;tick();
 esp32_mquickjs_wifi_mesh_job_status_t result;
 esp32_mquickjs_wifi_mesh_job_result(test_job,&result,NULL);
 assert(result.done&&result.dispatched&&result.returned&&result.timed_out);
 assert(result.native_error==-77&&result.error==ESP_ERR_TIMEOUT);
 assert(result.completed_us==native_time&&!test_session->job);
 esp32_mquickjs_wifi_mesh_job_release(test_job);
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(!live_allocations);
}
