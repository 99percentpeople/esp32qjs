
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define ESP_WIFI_MAX_SVC_NAME_LEN 256
#define ESP_WIFI_NAN_MAX_SVC_SUPPORTED 2
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 101
#define ESP_ERR_INVALID_STATE 102
#define ESP_ERR_NOT_FOUND 107
#define ESP_ERR_NOT_SUPPORTED 105
typedef int esp_err_t;
