
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
typedef int esp_err_t;
static bool on_wifi,dispatch_failure,time_disabled;
static int getter_error;
static unsigned calls,dispatches;
bool current_task_is_wifi_task(void){return on_wifi;}
unsigned esp32qjs_eap_native_resources(void){assert(on_wifi);return 13;}
int esp32qjs_eap_native_cleanup_error(void){assert(on_wifi);return 71;}
int esp32qjs_eap_native_control_error(void){assert(on_wifi);return 72;}
esp_err_t esp_eap_client_get_disable_time_check(bool *out){
    assert(on_wifi && out);++calls;*out=time_disabled;return getter_error;
}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){
    assert(!on_wifi);++dispatches;if(dispatch_failure)return ESP_FAIL;
    on_wifi=true;int result=fn(a,b);on_wifi=false;return result;
}
