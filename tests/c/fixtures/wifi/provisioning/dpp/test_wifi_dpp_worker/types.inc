
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
enum {ESP_DPP_AKM_DPP=1,ESP_DPP_AKM_PSK=2,ESP_DPP_AKM_SAE=3};
typedef int esp_err_t;
typedef struct {unsigned char secret[100];}wifi_config_t;
typedef struct {unsigned char secret[100];}esp_dpp_config_data_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FINISHED 0x10a
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
