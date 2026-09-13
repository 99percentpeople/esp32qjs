
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_NOT_SUPPORTED -2
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000
#define ESP32_MQUICKJS_WIFI_AP_DTIM_MAX 10
typedef int esp_err_t;
typedef struct { wifi_ap_config_t ap; } wifi_config_t;
static int unsupported;
static JSValue esp32_mquickjs_wifi_throw_configuration_error(JSContext *ctx,int error,const char *option,const void *execution) {
    (void)error;(void)option;assert(!execution);return JS_ThrowTypeError(ctx,"unsupported configuration");
}
static JSValue esp32_mquickjs_wifi_throw_operation_error(JSContext *ctx,const char *code,
    const char *operation,int error,int reason,uint32_t status) {
    assert(!strcmp(code,"WIFI_AP_UNSUPPORTED") && !strcmp(operation,"wifi.startAP"));
    assert(error==ESP_ERR_NOT_SUPPORTED);(void)reason;(void)status;unsupported++;
    return JS_ThrowTypeError(ctx,"unsupported AP configuration");
}
static JSValue get_property_with_fault(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr get_property_with_fault
