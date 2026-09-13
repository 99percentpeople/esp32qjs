
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t, wifi_mode_t, wifi_storage_t;
typedef struct {int sentinel;} esp_netif_t;
typedef struct {uint32_t generation,identity;int client;bool acquired;} esp32_mquickjs_wifi_radio_lease_t;
typedef struct {uint32_t generation,lease_identity,identity;int kind;} esp32_mquickjs_wifi_radio_operation_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 11
#define ESP_ERR_INVALID_STATE 12
#define ESP_ERR_INVALID_RESPONSE 13
#define ESP_ERR_TIMEOUT 14
#define ESP_ERR_WIFI_NOT_STARTED 15
#define ESP_ERR_NOT_SUPPORTED 16
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define WIFI_MODE_NAN 4
#define WIFI_MODE_STA 1
#define RADIO_EVENTS_IDLE 0
#define RADIO_EVENTS_STOP 2
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 1
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPING 2
#define ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN 3
typedef struct wifi_radio_nan wifi_radio_nan_t;
