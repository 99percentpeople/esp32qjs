
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_USD_ENABLE 1
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define NAN_PUBLISH_UNSOLICITED 1
#define NAN_PUBLISH_SOLICITED 2
#define NAN_SUBSCRIBE_ACTIVE 3
#define NAN_SUBSCRIBE_PASSIVE 4
typedef struct {uint32_t ghz_2_channels,ghz_5_channels;}wifi_scan_channel_bitmap_t;
typedef struct {uint8_t usd_default_channel,n_min,n_max,m_min,m_max;wifi_scan_channel_bitmap_t usd_chan_bitmap;}wifi_nan_usd_config_t;
typedef struct {char service_name[256];int type;uint32_t ttl;uint8_t*ssi;uint16_t ssi_len;
 wifi_nan_usd_config_t usd_publish_config,usd_subscribe_config;}wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
typedef struct {uint8_t inst_id,peer_inst_id,peer_mac[6];uint16_t ssi_len;uint8_t*ssi;void*vendor_ie;}wifi_nan_followup_params_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
typedef enum {ESP32_MQUICKJS_NAN_SDK_EVENT,ESP32_MQUICKJS_NAN_SDK_STARTED,
 ESP32_MQUICKJS_NAN_SDK_STOPPED,ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND}esp32_mquickjs_wifi_nan_sdk_notice_kind_t;
typedef struct {esp32_mquickjs_wifi_nan_sdk_notice_kind_t kind;int32_t event_id,status;uint8_t service_id;
 const void*data;size_t size;uint32_t context;}esp32_mquickjs_wifi_nan_sdk_notice_t;
#define ETH_ALEN 6
#define WIFI_IF_STA 0
#define WIFI_EVENT 9
#define WIFI_EVENT_ACTION_TX_STATUS 1
#define WIFI_EVENT_ROC_DONE 2
#define WIFI_EVENT_STA_STOP 3
