
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static unsigned depth,cancel_calls,resets;
static int cancel_result=-72;
static bool ndp_busy;
static bool esp32_mquickjs_wifi_nan_ndp_service_busy(uint8_t id){assert(id==7);return ndp_busy;}
static bool s_usd_in_progress;
static void *s_nan_data_lock=(void*)1,*nan_event_group=(void*)2;
struct own_svc_info{uint8_t type;};
static struct own_svc_info own_service={ESP_NAN_PUBLISH};
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static struct own_svc_info *nan_find_own_svc(uint8_t id){assert(depth==1);return id==7?&own_service:NULL;}
static void nan_reset_service(uint8_t id,bool all){assert(depth==1&&id==7&&!all);++resets;}
static int native_cancel(void *config,uint8_t *id,bool cancel){
 assert(!depth&&!config&&id&&*id==7&&cancel);++cancel_calls;
 NAN_DATA_LOCK();NAN_DATA_UNLOCK();return cancel_result;
}
static int esp_nan_internal_publish_service(void*c,uint8_t*i,bool x){return native_cancel(c,i,x);}
static int esp_nan_internal_subscribe_service(void*c,uint8_t*i,bool x){return native_cancel(c,i,x);}
