
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical,sdk_calls,captured;
#define portENTER_CRITICAL(p) do {(void)(p);assert(!critical);critical=1;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(critical);critical=0;} while(0)
typedef void (*sdk_callback_t)(void *,wifi_vendor_ie_type_t,const uint8_t[6],const vendor_ie_data_t *,int);
static sdk_callback_t sdk_callback;static void *sdk_context;
static int sdk_error;
static bool synchronous,unregister_inside;
esp_err_t esp32_mquickjs_wifi_vendor_ie_broker_unregister(uint32_t generation);
static int esp_wifi_set_vendor_ie_cb(sdk_callback_t cb,void *ctx) {
    assert(!critical);++sdk_calls;sdk_callback=cb;sdk_context=ctx;
    if(synchronous && cb)cb(ctx,WIFI_VND_IE_TYPE_BEACON,(void *)1,(void *)1,-42);
    return sdk_error;
}
void esp32_mquickjs_wifi_vendor_ie_watch_capture(uint32_t generation,wifi_vendor_ie_type_t frame,
        const uint8_t address[6],const vendor_ie_data_t *data,int rssi) {
    (void)frame;(void)address;(void)data;assert(!critical && generation && rssi==-42);++captured;
    if(unregister_inside)assert(esp32_mquickjs_wifi_vendor_ie_broker_unregister(generation)==ESP_ERR_TIMEOUT);
}
