
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_WPS_REGISTRAR 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define WPS_TYPE_DISABLE 0
#define WPS_STATUS_DISABLE 0
#define WIFI_PASSWORD_LEN_MAX 65
#define WIFI_APPIE_WPA 1
#define WIFI_APPIE_ASSOC_RESP 2
#define MSG_ERROR 0
#define wpa_printf(...) ((void)0)
enum{WPS_OWNER_NONE,WPS_OWNER_ENROLLEE,WPS_OWNER_REGISTRAR};
struct hostapd_config{struct{char *wpa_passphrase;}ssid;};
struct hostapd_data{struct hostapd_config *conf;void *wpa_auth;};
static struct hostapd_data *global_hapd;
static int close_error,frees,unset,disable_calls,public_disable,owner=WPS_OWNER_REGISTRAR;
static bool bound=true;
static int wps_get_owner(void){return owner;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return bound && p==global_hapd;}
static struct hostapd_data *hostapd_get_hapd_data(void){return global_hapd;}
static int wifi_ap_wps_disable_internal(void){disable_calls++;if(!close_error){owner=WPS_OWNER_NONE;bound=false;}return close_error;}
static int esp_wifi_ap_wps_disable(void){public_disable++;return close_error;}
static int esp_wifi_get_wps_type_internal(void){return WPS_TYPE_DISABLE;}
static int esp_wifi_get_wps_status_internal(void){return WPS_STATUS_DISABLE;}
static int esp_wifi_unset_appie_internal(int id){(void)id;unset++;return 0;}
static void os_free(void *p){if(p==global_hapd)frees++;}
static void wpa_deinit(void *p){(void)p;}
static void forced_memzero(void *p,size_t n){(void)p;(void)n;}
static void hostapd_config_free_bss(void *p){(void)p;}
