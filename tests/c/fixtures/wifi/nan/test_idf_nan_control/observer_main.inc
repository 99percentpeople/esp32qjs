
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static bool entered,proceed;static unsigned count;static uint32_t current;
static void receive_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t *notice,void *p){
    assert(!observer_depth&&p==&count&&identity==current&&notice->ndp_id==9);
    pthread_mutex_lock(&mutex);++count;entered=true;pthread_cond_broadcast(&condition);
    while(!proceed){pthread_cond_wait(&condition,&mutex);}pthread_mutex_unlock(&mutex);
}
static void *deliver(void *p){(void)p;esp32qjs_nan_control(ESP32_MQUICKJS_NAN_SDK_NDP_TERMINATED,0,9,1,0,NULL);return NULL;}
int main(void){
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_OK);
    uint32_t stale=current,next=0;pthread_t sender;assert(!pthread_create(&sender,NULL,deliver,NULL));
    pthread_mutex_lock(&mutex);while(!entered)pthread_cond_wait(&condition,&mutex);pthread_mutex_unlock(&mutex);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_ERR_TIMEOUT&&current==stale);
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&next)==ESP_ERR_INVALID_STATE&&!next);
    deliver(NULL);assert(count==1);
    pthread_mutex_lock(&mutex);proceed=true;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&mutex);
    assert(!pthread_join(sender,NULL));assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_OK&&!current);
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_OK&&current!=stale);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_OK);
    s_esp32qjs_nan_observer.next_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_OK&&current==UINT32_MAX);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&current)==ESP_OK);
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&count,&current)==ESP_ERR_NO_MEM&&!current);
}
