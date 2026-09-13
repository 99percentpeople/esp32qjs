
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_RESPONSE -3
#define ESP_ERR_NOT_SUPPORTED -4
#define ESP_ERR_NO_MEM -5
#define ESP_ERR_NOT_ALLOWED -6
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_PROTOCOL_LR 8
#define WIFI_PROTOCOL_11A 16
#define WIFI_PROTOCOL_11AC 32
#define WIFI_PROTOCOL_11AX 64
#define WIFI_RADIO_MAX_LEASES 2
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 2
typedef int esp_err_t;
