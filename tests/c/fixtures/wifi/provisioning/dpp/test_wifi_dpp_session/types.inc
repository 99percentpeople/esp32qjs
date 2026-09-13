
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP_DPP_MAX_CONFIG_COUNT 3
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_INVALID_SIZE 6
#define ESP_FAIL 7
#define ESP_ERR_INVALID_RESPONSE 8
#define ESP_ERR_NOT_SUPPORTED 9
enum {ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS=1,ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_FAILURE,ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT};
#define MALLOC_CAP_8BIT 1
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE;
typedef struct {int unused;} wifi_config_t;
typedef struct {bool valid;uint8_t ssid[32],ssid_len,bssid[6],channel;uint16_t aid;}esp32_mquickjs_wifi_link_snapshot_t;
typedef struct {uint8_t ssid[32],ssid_len,key[128];} esp_dpp_config_data_t;
typedef struct {int unused;} wifi_event_action_tx_status_t;
typedef struct {uint32_t generation,identity;unsigned client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;unsigned kind;} esp32_mquickjs_wifi_radio_operation_t;
