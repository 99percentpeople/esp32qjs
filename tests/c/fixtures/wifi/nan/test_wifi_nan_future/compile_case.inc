
typedef int JSValue,JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
#define JS_NULL 0
static bool wait_timeout_seen;
#if CONFIG_ESP_WIFI_NAN_PAIRING
static JSValue nan_pairing_error(JSContext*ctx,esp32_mquickjs_wifi_nan_pairing_t*p,const char*name,int error,bool timeout){
 (void)ctx;assert(p&&name&&error);wait_timeout_seen=timeout;return -1;
}
#endif
static JSValue nan_path_error(JSContext *ctx,esp32_mquickjs_wifi_nan_path_t *s,const char *name,int error,bool timeout){
 (void)ctx;assert(s&&name&&error);wait_timeout_seen=timeout;return -1;
}
static JSValue nan_message_error(JSContext *ctx,esp32_mquickjs_wifi_nan_message_t *s,int error,bool timeout) {
    (void)ctx;assert(s&&error);wait_timeout_seen=timeout;return -1;
}
static JSValue nan_error(JSContext *ctx,const char *name,esp32_mquickjs_wifi_nan_session_t *s,int error,bool timeout) {
    (void)ctx;assert(name&&s&&error==ESP_ERR_TIMEOUT);wait_timeout_seen=timeout;return -1;
}
static JSValue nan_service_error(JSContext *ctx,const char *name,esp32_mquickjs_wifi_nan_discovery_t *s,int error,bool timeout) {
    (void)ctx;assert(name&&s&&error==ESP_ERR_TIMEOUT);wait_timeout_seen=timeout;return -1;
}
