
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t,esp_event_base_t;
typedef unsigned TickType_t;
typedef struct {int unused;} esp32_mquickjs_runtime_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
typedef int wifi_mode_t;
#define WIFI_EVENT_STA_START 10
#define WIFI_EVENT_STA_STOP 11
#define WIFI_EVENT_AP_START 12
#define WIFI_EVENT_AP_STOP 13
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT 7
#define WIFI_RADIO_START_EVENT_TIMEOUT_MS 8
#define WIFI_RADIO_STOP_EVENT_TIMEOUT_MS 8
#define pdMS_TO_TICKS(v) (v)
