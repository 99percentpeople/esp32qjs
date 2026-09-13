
typedef struct{int marker;}wifi_nan_publish_cfg_t,wifi_nan_subscribe_cfg_t;
static unsigned publish_calls,cancel_calls;static int cancel_error,resume_error;static bool tx_drained;
static int esp32_mquickjs_wifi_nan_sdk_publish(const wifi_nan_publish_cfg_t*p,uint8_t*id){sdk_boundary();assert(p&&id&&!*id);++publish_calls;*id=7;return 0;}
static int esp32_mquickjs_wifi_nan_sdk_subscribe(const wifi_nan_subscribe_cfg_t*p,uint8_t*id){return esp32_mquickjs_wifi_nan_sdk_publish(p,id);}
static int esp32_mquickjs_wifi_nan_sdk_cancel_service(uint8_t id){sdk_boundary();assert(id==7);++cancel_calls;return cancel_error;}
static bool esp32_mquickjs_wifi_nan_tx_service_drained(uint8_t id){sdk_boundary();assert(id==7);return tx_drained;}
static int esp32_mquickjs_wifi_nan_sdk_service_resume(uint8_t id){sdk_boundary();assert(id==7);return resume_error;}
