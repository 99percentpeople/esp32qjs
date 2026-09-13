
static uint8_t tx_buffer[80],tx_metadata[32];
static bool tail_entered,tail_continue;
static unsigned tail_writes,action_completions;
void __real_nan_sdf_txcb(void *buffer){
 assert(buffer==tx_buffer&&!observer_depth);
 if(tx_metadata[9]&0x7f){++action_completions;return;}
 struct nan_cb_peer_info info={.peer_svc_id=2,.peer_mac={1,2,3,4,5,6}};
 nan_app_replied_cb(7,&info);
 pthread_mutex_lock(&free_mutex);tail_entered=true;pthread_cond_broadcast(&free_condition);
 while(!tail_continue)pthread_cond_wait(&free_condition,&free_mutex);
 ++tail_writes;pthread_mutex_unlock(&free_mutex);
}
static void *deliver_native(void *p){(void)p;__wrap_nan_sdf_txcb(tx_buffer);return NULL;}
int main(void){
 uint8_t *metadata_pointer=tx_metadata;memcpy(tx_buffer+56,&metadata_pointer,sizeof(metadata_pointer));tx_metadata[8]=7;
 free_continue=true;assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 pthread_t task;assert(!pthread_create(&task,NULL,deliver_native,NULL));
 pthread_mutex_lock(&free_mutex);while(!tail_entered)pthread_cond_wait(&free_condition,&free_mutex);pthread_mutex_unlock(&free_mutex);
 esp32_mquickjs_wifi_nan_sdk_observer_status_t status;esp32_mquickjs_wifi_nan_sdk_observer_status(&status);
 assert(posted==1&&!status.callbacks&&status.service_callbacks==1&&!tail_writes);
 assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(7)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_nan_sdk_service_resume(7)==ESP_ERR_INVALID_STATE);
 __wrap_nan_sdf_txcb(tx_buffer);assert(posted==1&&!tail_writes);
 pthread_mutex_lock(&free_mutex);tail_continue=true;pthread_cond_broadcast(&free_condition);pthread_mutex_unlock(&free_mutex);
 assert(!pthread_join(task,NULL));assert(tail_writes==1);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
 __wrap_nan_sdf_txcb(tx_buffer);assert(posted==1&&tail_writes==1);
 tx_metadata[9]=2;__wrap_nan_sdf_txcb(tx_buffer);assert(action_completions==1);
 tx_metadata[9]=0;assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 __wrap_nan_sdf_txcb(tx_buffer);assert(posted==2&&tail_writes==2);
 esp32_mquickjs_wifi_nan_sdk_observer_status(&status);assert(!status.service_callbacks);
}
