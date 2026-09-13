
typedef int esp_err_t;
#define ESP_OK 0
static unsigned native_reads;
static int esp32_mquickjs_wifi_radio_read_he_statistics(esp32_mquickjs_wifi_he_statistics_t *actual,const char **stage){
    ++native_reads;*actual=(esp32_mquickjs_wifi_he_statistics_t){true,false,9};*stage=NULL;return 0;}
static JSValue driver_read_error(JSContext *ctx,const char *iface,const char *op,int error,const char *stage){
    (void)iface;return JS_ThrowTypeError(ctx,"%s %d %s",op,error,stage);}
