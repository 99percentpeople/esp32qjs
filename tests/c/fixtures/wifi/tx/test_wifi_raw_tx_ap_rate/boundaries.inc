
#include <stdlib.h>
#define ESP_ERR_INVALID_RESPONSE 80
static struct {struct {uint32_t identity;} start_owner;} s_vendor_ie;
static struct {void *snapshot;} s_config_restart;
static struct {struct {uint32_t identity;} owner;} s_policy_restart;
static wifi_config_t stored;
static unsigned gets,allocations,frees,wipes;
static int config_error,mode_error,validation_error;
static bool allocation_fail;
static void *ap_calloc(size_t count,size_t size) {
    if(allocation_fail)return NULL;
    ++allocations;return calloc(count,size);
}
static void ap_free(void *p) {
    assert(p);const unsigned char *bytes=p;
    for(size_t i=0;i<sizeof(wifi_config_t);++i)assert(!bytes[i]);
    ++frees;free(p);
}
#define calloc ap_calloc
#define free ap_free
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t size) {
    assert(size==sizeof(wifi_config_t));memset(p,0,size);++wipes;
}
static esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
    assert(locks && !critical);*mode=s_radio.effective_mode;return mode_error;
}
static esp_err_t esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(locks && !critical && interface==WIFI_IF_AP);++gets;*config=stored;return config_error;
}
/* Complete AP credential validation has its own production fixture. */
static esp_err_t esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config) {
    assert(config);return validation_error;
}
