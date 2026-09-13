
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
typedef struct { int unused; } esp_netif_t;
typedef struct { int unused; } esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_STA() ((esp_netif_config_t){0})
static struct { esp_netif_t *sta_netif; int sta_detach_error; } s_wifi_state;
static const char *s_wifi_setup_stage;
static esp_netif_t netif_object;
static int failed, calls[5], destroyed;
static esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg) {
    assert(cfg);calls[1]++;return failed==1?NULL:&netif_object;
}
static int esp_netif_attach_wifi_station(esp_netif_t *netif) {
    assert(netif==&netif_object);calls[2]++;return failed==2?-2:0;
}
static int esp_wifi_set_default_wifi_sta_handlers(void) {
    calls[3]++;return failed==3?-3:0;
}
static int esp_wifi_clear_default_wifi_driver_and_handlers(esp_netif_t *netif) {
    assert(netif==&netif_object);calls[4]++;return failed==4?-4:0;
}
static void esp_netif_destroy(esp_netif_t *netif) { assert(netif==&netif_object);destroyed++; }
/* SDK-facing retirement boundary; its scheduler is covered separately by
 * test_wifi_netif_retirement using the complete production translation unit. */
static int esp32_mquickjs_wifi_netif_retire(esp_netif_t **n,int *error) {
    if(*error)return *error;
    if(!*n)return ESP_OK;
    *error=esp_wifi_clear_default_wifi_driver_and_handlers(*n);
    if(!*error){esp_netif_destroy(*n);*n=NULL;}
    return *error;
}
