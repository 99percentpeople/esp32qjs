
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define CONFIG_ESP_WIFI_NAN_USD_ENABLE 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_OFFCHAN_TX_REQ 1
#define WIFI_OFFCHAN_TX_CANCEL 2
#define WIFI_ROC_CANCEL 2
#define WIFI_ROC_DONE 0
#define WIFI_ACTION_TX_DONE 1
#define WIFI_ACTION_TX_FAILED 2
#define WIFI_ACTION_TX_DURATION_COMPLETED 3
#define WIFI_ACTION_TX_OP_CANCELLED 4
typedef int esp_err_t,wifi_roc_done_status_t;
typedef struct {int unused;}wifi_nan_followup_params_t;
typedef struct {int unused;}wifi_nan_publish_cfg_t;
typedef struct {int unused;}wifi_nan_subscribe_cfg_t;
typedef struct {int unused;}wifi_nan_usd_config_t;
typedef struct {int ifx,type,sec_channel;uint8_t channel,op_id;
 int(*rx_cb)(uint8_t*,uint8_t*,size_t,uint8_t);uint32_t data_len;uint8_t data[];}wifi_action_tx_req_t;
typedef struct {int ifx,type,sec_channel;uint8_t channel,op_id;
 int(*rx_cb)(uint8_t*,uint8_t*,size_t,uint8_t);void(*done_cb)(uint32_t,uint8_t,wifi_roc_done_status_t);}wifi_roc_req_t;
typedef struct {int ifx,status;uintptr_t context;uint8_t channel,op_id;}wifi_event_action_tx_status_t;
