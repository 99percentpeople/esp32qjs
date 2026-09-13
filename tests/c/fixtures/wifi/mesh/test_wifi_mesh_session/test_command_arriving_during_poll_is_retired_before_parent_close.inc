
int main(void){
 test_session=open_session();enqueue_then_close=true;tick();
 assert(test_job&&test_job->status.done&&test_job->status.cancelled);
 assert(!test_job->status.dispatched&&!radio_sends&&!test_session->job);
 assert(test_session->status.retired&&!s_mesh_active&&radio_closes==1);
 esp32_mquickjs_wifi_mesh_job_release(test_job);
 esp32_mquickjs_wifi_mesh_session_release(test_session);
 assert(!s_mesh_jobs&&!s_mesh_handles&&!live_allocations);
}
