
static uint32_t old_identity;
static struct nan_de*old_engine;
static pthread_barrier_t pending,dispatch;
static void*late_timer(void*unused){
 (void)unused;pthread_barrier_wait(&pending);pthread_barrier_wait(&dispatch);
 assert(!esp32qjs_nan_usd_timer_enter(old_engine,old_identity));return NULL;
}
int main(void){
 assert(!esp_nan_usd_init());old_engine=g_nan_de;old_identity=s_esp32qjs_usd_identity;
 pthread_barrier_init(&pending,NULL,2);pthread_barrier_init(&dispatch,NULL,2);
 pthread_t t;assert(!pthread_create(&t,NULL,late_timer,NULL));pthread_barrier_wait(&pending);
 assert(!esp_nan_usd_deinit()&&!esp_nan_usd_init());assert(g_nan_de==old_engine);
 assert(s_esp32qjs_usd_identity!=old_identity);
 pthread_barrier_wait(&dispatch);assert(!pthread_join(t,NULL));
 assert(esp32qjs_nan_usd_timer_enter(g_nan_de,s_esp32qjs_usd_identity));esp32qjs_nan_usd_timer_leave();
 esp32qjs_nan_usd_timer_failed(g_nan_de);
 esp32_mquickjs_wifi_nan_usd_sdk_status_t status;esp32_mquickjs_wifi_nan_usd_sdk_status(&status);
 assert(status.error==ESP_ERR_NO_MEM&&status.closing);
 assert(!esp32qjs_nan_usd_timer_enter(g_nan_de,s_esp32qjs_usd_identity));
 assert(!esp_nan_usd_deinit());s_esp32qjs_usd_next_identity=UINT32_MAX;
 assert(!esp_nan_usd_init()&&s_esp32qjs_usd_identity==UINT32_MAX);
 assert(!esp_nan_usd_deinit());unsigned created=engine_creates;
 assert(esp_nan_usd_init()==ESP_ERR_NO_MEM&&!g_nan_de&&engine_creates==created);
}
