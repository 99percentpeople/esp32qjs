
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define CONFIG_ESP_WIFI_ENABLE_WPA3_SAE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS 1
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE 2
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT 3
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WPA3_SAE_PWE_BOTH 3
#define MALLOC_CAP_8BIT 1
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE,wifi_second_chan_t;
typedef enum {WIFI_AUTH_OPEN,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA3_PSK} wifi_auth_mode_t;
typedef struct {struct {
 uint8_t ssid[32],password[64],bssid[6];bool bssid_set;
 int scan_method,sort_method,sae_pwe_h2e;
 struct {int rssi;wifi_auth_mode_t authmode;} threshold;
 struct {bool capable,required;} pmf_cfg;
} sta;} wifi_config_t;
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {uint8_t ssid[32],password[64];bool bssid_set;uint8_t bssid[6];smartconfig_type_t type;uint8_t token,cellphone_ip[4];} smartconfig_event_got_ssid_pswd_t;
typedef struct {uint32_t generation,identity;unsigned client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;unsigned kind;} esp32_mquickjs_wifi_radio_operation_t;
