
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define NAN_STARTED_BIT 1
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
#define NAN_SVC_ID_PENDING 255
#define ESP_WIFI_MAX_SVC_SSI_LEN 512
#define WIFI_SVC_PROTO_MAX 16
typedef struct {uint8_t oui[3],proto;}wifi_nan_wfa_ssi_t;
typedef struct {unsigned ignored;}pairing_t;
typedef struct {
 char service_name[256],matching_filter[256];
 uint8_t *ssi;uint16_t ssi_len;pairing_t *pairing;void *security_cfg;
 bool usd_discovery_flag,security_reqd,ndp_resp_needed;
}wifi_nan_publish_cfg_t;
typedef wifi_nan_publish_cfg_t wifi_nan_subscribe_cfg_t;
static unsigned depth,allocations,fail_at,heap_live,submits;
static bool claimed,s_usd_in_progress;
static int native_error=-72;
static void *s_nan_data_lock=(void*)1,*nan_event_group=(void*)2;
static struct{unsigned state;}s_nan_ctx={NAN_STARTED_BIT};
static uint8_t s_wfa_oui[3]={0x50,0x6f,0x9a};
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static bool nan_services_limit_reached(void){assert(depth==1);return false;}
static void *nan_find_own_svc_by_name(const char*n){assert(depth==1&&n);return NULL;}
static void *os_zalloc(size_t size){if(++allocations==fail_at)return NULL;void*p=calloc(1,size);assert(p);++heap_live;return p;}
static void *os_malloc(size_t size){return os_zalloc(size);}
static void os_free(void *p){assert(p&&heap_live);--heap_live;free(p);}
static bool nan_compute_service_id(const char*n,uint8_t *hash){assert(n&&hash&&depth==1);memset(hash,1,6);return true;}
static bool nan_claim_own_svc_slot(unsigned type,const char*n,void*a,void*b){assert(type&&n&&!a&&depth==1);(void)b;claimed=true;return true;}
static void nan_abort_own_svc(const char*n){assert(n&&claimed&&depth==1);claimed=false;}
static void nan_finalize_own_svc(const char*n,uint8_t id,bool respond,uint8_t *hash){assert(n&&(id==7||id==NAN_SVC_ID_PENDING)&&hash&&claimed&&depth==1);(void)respond;}
static int native_submit(void *config,uint8_t *id,bool cancel){
 assert(!depth&&claimed&&config&&!cancel);++submits;
 /* A callback on the WiFi task must be allowed to acquire its data mutex. */
 NAN_DATA_LOCK();assert(claimed);NAN_DATA_UNLOCK();
 if(!native_error)*id=7;
 return native_error;
}
static int esp_nan_internal_publish_service(void*c,uint8_t*i,bool x){return native_submit(c,i,x);}
static int esp_nan_internal_subscribe_service(void*c,uint8_t*i,bool x){return native_submit(c,i,x);}
