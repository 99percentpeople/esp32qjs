
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define MALLOC_CAP_8BIT 1
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE,wifi_second_chan_t;
typedef struct {uint32_t generation,identity;unsigned client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;unsigned kind;} esp32_mquickjs_wifi_radio_operation_t;
