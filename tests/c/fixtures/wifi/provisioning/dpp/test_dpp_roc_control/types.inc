
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
typedef int wifi_roc_done_status_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
typedef struct {char unused;}esp_dpp_config_data_t;
typedef void (*eloop_timeout_handler)(void*,void*);
typedef void (*wifi_action_roc_done_cb_t)(uint32_t,uint8_t,wifi_roc_done_status_t);
typedef struct {
 int ifx,type,sec_channel;uint8_t op_id,channel;void *rx_cb;
 wifi_action_roc_done_cb_t done_cb;
}wifi_roc_req_t;
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_ROC_CANCEL 0
#define WIFI_ROC_REQ 1
#define WIFI_ROC_DONE 0
#define WIFI_ROC_FAIL 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_TIMEOUT 4
#define DPP_ROC_EVENT_HANDLED 1
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical,depth;
#define portENTER_CRITICAL(lock) do{(void)(lock);assert(!critical);critical++;}while(0)
#define portEXIT_CRITICAL(lock) do{(void)(lock);assert(critical==1);critical--;}while(0)
