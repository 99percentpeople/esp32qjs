
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
typedef int esp_err_t;
typedef int wifi_action_tx_status_type_t;
typedef struct {char unused;}esp_dpp_config_data_t;
typedef void (*eloop_timeout_handler)(void*,void*);
typedef struct {int ifx,type,sec_channel;uint8_t op_id,channel;void*rx_cb;uint32_t data_len;
 _Alignas(8) uint8_t data[0];}wifi_action_tx_req_t;
typedef struct {int ifx;uint32_t context;int status;uint8_t op_id,channel;}wifi_event_action_tx_status_t;
enum dpp_tx_frame_type {DPP_TX_INVALID,DPP_TX_GAS_CONFIG_REQ};
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_OFFCHAN_TX_CANCEL 0
#define WIFI_OFFCHAN_TX_REQ 1
#define WIFI_ACTION_TX_DONE 0
#define WIFI_ACTION_TX_FAILED 1
#define WIFI_ACTION_TX_DURATION_COMPLETED 2
#define WIFI_ACTION_TX_OP_CANCELLED 3
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_INVALID_ARG 5
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical,depth;
#define portENTER_CRITICAL(lock) do{(void)(lock);assert(!critical);critical++;}while(0)
#define portEXIT_CRITICAL(lock) do{(void)(lock);assert(critical==1);critical--;}while(0)
