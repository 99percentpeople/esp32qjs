
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static const char *esp_err_to_name(int e){(void)e;return "injected";}
void esp32_mquickjs_wifi_nan_tx_status(esp32_mquickjs_wifi_nan_tx_status_t *s){memset(s,0,sizeof(*s));}
void esp32_mquickjs_wifi_nan_sdk_observer_status(esp32_mquickjs_wifi_nan_sdk_observer_status_t *s){memset(s,0,sizeof(*s));}
