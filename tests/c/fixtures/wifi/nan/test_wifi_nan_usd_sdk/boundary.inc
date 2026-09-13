
struct nan_de{int dummy;};
static struct nan_de engine,*g_nan_de;
static _Atomic(void*)s_nan_usd_data_lock;
static pthread_mutex_t mutex;
static unsigned register_calls,unregister_calls,fail_register,fail_unregister;
static unsigned engine_creates,engine_frees,post_calls;
static bool fail_engine,transport_busy;
typedef struct {esp32_mquickjs_wifi_nan_message_tx_status_t message;bool message_scope;esp_err_t error;}esp32qjs_usd_transport_t;
static esp32qjs_usd_transport_t transport_storage,*s_esp32qjs_usd_transport;
static int esp32qjs_nan_usd_transport_open(void){assert(!s_esp32qjs_usd_transport);memset(&transport_storage,0,sizeof(transport_storage));s_esp32qjs_usd_transport=&transport_storage;return 0;}
static int esp32qjs_nan_usd_transport_free(void){assert(!transport_busy);s_esp32qjs_usd_transport=NULL;return 0;}
static void esp32qjs_nan_usd_transport_snapshot(esp32_mquickjs_wifi_nan_usd_sdk_status_t*s){s->transport_held=transport_busy;}
esp_err_t esp32_mquickjs_wifi_nan_usd_sdk_poll(bool close){assert(close);return transport_busy?ESP_ERR_NOT_FINISHED:0;}
static void esp32_mquickjs_wifi_nan_sdk_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t*notice){assert(notice);}
static int esp32qjs_nan_usd_service_drain(uint8_t id){assert(id);return transport_busy?ESP_ERR_NOT_FINISHED:0;}
static int mac_error,post_error;
static _Thread_local unsigned lock_depth;
static void*os_recursive_mutex_create(void){
 pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);
 pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
 assert(!pthread_mutex_init(&mutex,&attr));pthread_mutexattr_destroy(&attr);return &mutex;
}
#define NAN_USD_DATA_LOCK() do{pthread_mutex_lock(s_nan_usd_data_lock);++lock_depth;}while(0)
#define NAN_USD_DATA_UNLOCK() do{assert(lock_depth);--lock_depth;pthread_mutex_unlock(s_nan_usd_data_lock);}while(0)
typedef void(*handler_t)(void*,int,int,void*);
static void nan_sta_stop_handler(void*a,int b,int c,void*d){(void)a;(void)b;(void)c;(void)d;}
static void nan_de_tx_event_handler(void*a,int b,int c,void*d){(void)a;(void)b;(void)c;(void)d;}
static int esp_event_handler_register(int base,int event,handler_t handler,void*arg){
 assert(!lock_depth&&base==WIFI_EVENT&&event>=1&&event<=3&&handler&&!arg);
 return ++register_calls==fail_register?-71:0;
}
static int esp_event_handler_unregister(int base,int event,handler_t handler){
 assert(!lock_depth&&base==WIFI_EVENT&&event>=1&&event<=3&&handler);
 return ++unregister_calls==fail_unregister?-72:0;
}
static int esp_event_post(int base,int event,const void*data,size_t size,unsigned wait){
 (void)data;(void)size;assert(lock_depth&&base==WIFI_EVENT&&event==4&&!wait);++post_calls;return post_error;
}
static int esp_wifi_get_mac(int interface,uint8_t*mac){assert(!lock_depth&&!interface);memset(mac,2,6);return mac_error;}
static void esp_nan_de_tx(void){}
static void esp_nan_de_listen(void){}
static void esp_nan_de_discovery_result(void){}
static void esp_nan_de_replied(void){}
static void esp_nan_de_publish_terminated(void){}
static void esp_nan_de_subscribe_terminated(void){}
static void esp_nan_de_receive(void){}
struct nan_callbacks{void(*tx)(void),(*listen)(void),(*discovery_result)(void),(*replied)(void),
 (*publish_terminated)(void),(*subscribe_terminated)(void),(*receive)(void);};
static struct nan_de*nan_de_init(uint8_t*mac,bool offload,bool ap,unsigned listen,struct nan_callbacks*cb){
 assert(lock_depth&&mac&&!offload&&!ap&&listen==1000&&cb->tx==esp_nan_de_tx);++engine_creates;
 return fail_engine?NULL:&engine;
}
static void nan_de_deinit(struct nan_de*de){assert(lock_depth&&de==&engine);++engine_frees;}
