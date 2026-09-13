
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
typedef int esp_event_base_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
typedef struct {unsigned char unused[16];}wifi_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_NOT_FINISHED 5
#define MAX_SSID_LEN 32
#define MAX_PASSPHRASE_LEN 64
#define ESP_DPP_MAX_CONNECTOR_LEN 512
#define ESP_DPP_MAX_KEY_LEN 128
#define ESP_DPP_MAX_CONFIG_COUNT 3
#define ESP_DPP_AKM_DPP 1
#define ESP_DPP_AKM_PSK_SAE_DPP 6
#define WIFI_EVENT 1
#define WIFI_EVENT_DPP_FAILED 2
#define WIFI_EVENT_DPP_CFG_RECVD 3
#define WIFI_EVENT_ACTION_TX_STATUS 4
#define WIFI_EVENT_ROC_DONE 5
#define WIFI_IF_STA 0
#define WIFI_ROC_CANCEL 1
#define WIFI_OFFCHAN_TX_CANCEL 2
#define WIFI_SECOND_CHAN_NONE 0
#define WPS_OWNER_NONE 0
#define CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR 0
#define ESP_EVENT_DEFINE_BASE(name) static const int name=100
#define os_memcpy memcpy
#define os_bzero(p,n) memset(p,0,n)
enum dpp_akm {DPP_AKM_UNKNOWN,DPP_AKM_DPP,DPP_AKM_PSK,DPP_AKM_SAE,DPP_AKM_PSK_SAE,DPP_AKM_SAE_DPP,DPP_AKM_PSK_SAE_DPP};
typedef struct {int ifx,type,sec_channel;void *rx_cb;uint8_t op_id,channel;}wifi_roc_req_t;
typedef wifi_roc_req_t wifi_action_tx_req_t;
