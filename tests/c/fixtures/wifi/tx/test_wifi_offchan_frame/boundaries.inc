
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_WIFI_POST 0x3012
#define ESP_ERR_NO_MEM 6
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 0
#define CONFIG_SOC_WIFI_HE_SUPPORT 0
#define IRAM_ATTR
#define DRAM_ATTR
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical;
#define portENTER_CRITICAL_SAFE(m) do{(void)m;assert(!critical);critical++;}while(0)
#define portEXIT_CRITICAL_SAFE(m) do{(void)m;assert(critical==1);critical--;}while(0)
uint8_t g_offchan_ctx[28];
static unsigned posted,delivered,recycled;
static int output_error,last_status;
static uint8_t last_interface;
static bool output_completes,output_recycles,reuse_during_recycle;
static unsigned wakes;
static bool wake_fails;
#define WIFI_EVENT 1
#define WIFI_EVENT_ACTION_TX_STATUS 19
#define WIFI_EVENT_ROC_DONE 20
typedef struct {int ifx;uint32_t context;int status;uint8_t op_id,channel;}wifi_event_action_tx_status_t;
typedef struct {uint32_t context;int status;uint8_t op_id,channel;}wifi_event_roc_done_t;
static unsigned captured,observed,forwarded;
static bool esp32qjs_dpp_is_action_callback(uintptr_t callback){return callback==0x1234;}
static void esp32qjs_dpp_tx_status_capture(const wifi_event_action_tx_status_t*event){assert(!critical&&event->context==0x1234);captured++;}
static int esp_event_post(int base,int id,const void*data,size_t size,int wait){
 assert(!critical&&base==WIFI_EVENT&&data&&!wait);
 if(id==19){assert(size==sizeof(wifi_event_action_tx_status_t)&&captured>observed);observed++;}
 else assert(id==20&&size==sizeof(wifi_event_roc_done_t));
 return 88;
}
int wifi_event_post(int id,void*data,size_t size){(void)id;(void)data;(void)size;assert(!critical);forwarded++;return 77;}
int pp_post(int signal,void*message){assert(!critical&&signal==5&&!message);wakes++;return wake_fails?-1:0;}
