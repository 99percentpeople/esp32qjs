
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_WIFI_MAX_FUP_SSI_LEN 2048
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
typedef int esp_err_t;
#define NAN_VENDOR_IE_MAX_BODY_LEN 255
typedef struct{uint8_t vendor_oui[3];uint16_t body_len;uint8_t*body;}nan_vendor_ie_t;
typedef struct{uint8_t inst_id,peer_inst_id,peer_mac[6];uint16_t ssi_len;uint8_t*ssi;nan_vendor_ie_t*vendor_ie;}wifi_nan_followup_params_t;
static unsigned depth,calls;
static int native_error;
static bool peer_exists,s_usd_in_progress;
static void*s_nan_data_lock=(void*)1,*nan_event_group=(void*)1;
static uint8_t null_mac[6];
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static void*nan_find_own_svc(uint8_t id){assert(depth&&id==7);return (void*)1;}
static void*nan_find_peer_svc(uint8_t id,uint8_t peer,uint8_t*mac){assert(depth&&id==7&&peer==3&&mac[0]==2);return peer_exists?(void*)1:NULL;}
static int esp_nan_internal_send_followup(wifi_nan_followup_params_t*p,uint32_t*context,void*extra){
 assert(!depth&&p&&context&&!extra);++calls;if(!native_error)*context=1234;return native_error;
}
