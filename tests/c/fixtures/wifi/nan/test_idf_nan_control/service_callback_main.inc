
static void *deliver_replied(void *p){(void)p;struct nan_cb_peer_info info={.peer_svc_id=2,.peer_mac={1,2,3,4,5,6}};nan_app_replied_cb(7,&info);return NULL;}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(0));
 pthread_t task;assert(!pthread_create(&task,NULL,deliver_replied,NULL));
 pthread_mutex_lock(&free_mutex);while(!free_entered)pthread_cond_wait(&free_condition,&free_mutex);pthread_mutex_unlock(&free_mutex);
 esp32_mquickjs_wifi_nan_sdk_observer_status_t status;esp32_mquickjs_wifi_nan_sdk_observer_status(&status);
 assert(posted==1&&!status.callbacks&&status.service_callbacks==1);
 assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(7)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_nan_sdk_service_resume(7)==ESP_ERR_INVALID_STATE);
 deliver_replied(NULL);assert(posted==1); /* denied before touching SDK service storage */
 pthread_mutex_lock(&free_mutex);free_continue=true;pthread_cond_broadcast(&free_condition);pthread_mutex_unlock(&free_mutex);
 assert(!pthread_join(task,NULL));assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(7));
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(7));
 fail_allocation=true;deliver_replied(NULL);assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(7));
 assert(!esp32_mquickjs_wifi_nan_sdk_service_resume(7));nan_app_replied_cb(7,NULL);
 assert(!esp32_mquickjs_wifi_nan_sdk_service_quiesce(0));
}
