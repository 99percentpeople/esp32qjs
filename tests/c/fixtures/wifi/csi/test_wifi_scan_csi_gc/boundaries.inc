
#define WIFI_SCAN_PARAMS_DEFAULT_CONFIG() {.scan_time={.active={.min=0,.max=120},.passive=360},.home_chan_dwell_time=30}
#define ESP32_MQUICKJS_WIFI_CSI_HE_STBC_ALTERNATE 2
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP_ERR_NOT_SUPPORTED 3
typedef int esp_err_t;
static int esp_wifi_set_scan_parameters(const wifi_scan_default_params_t *p){(void)p;abort();}
static JSValue wifi_csi_throw_driver_error(JSContext *ctx,const char *code,const char *stage,esp_err_t error){
    return JS_ThrowTypeError(ctx,"%s %s %d",code,stage,error);}
