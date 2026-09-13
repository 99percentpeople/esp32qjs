
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_PAIRING 1
#define ESP_WIFI_NAN_NPK_LEN 32
#define ESP_WIFI_NAN_NIK_LEN 16
#define ESP_WIFI_NAN_NDP_PMK_LEN 32
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_WIFI_NAN_MAX_PEER_CREDS 2
#define WIFI_NAN_CSID_NCS_SK_128 1
#define WIFI_NAN_CSID_NCS_SK_256 2
#define MACADDR_LEN 6
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define ESP_LOGI(...) ((void)0)
#define NAN_APP_PEER_NIK_LEN 16
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define WPA_KEY_MGMT_SAE 7
#define NAN_PAIRING_PINCODE_MAX 999999
#define NAN_PAIRING_ROLE_INITIATOR 0
#define NAN_PAIRING_ROLE_RESPONDER 1
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
typedef int esp_err_t;
typedef struct {uint8_t peer_nik[16],npk[32],service_hash[6];bool is_valid;} wifi_nan_peer_creds_t;
typedef struct {uint8_t peer_svc_id,peer_nmi[6];int self_role;bool pairing_verification;struct {uint32_t pincode;} cred;} wifi_nan_pairing_config_t;
struct own_svc_info {uint8_t svc_id,svc_hash[6],verify_session_peer_nmi[6];bool verify_session_pending;
 struct {bool pairing_setup,pairing_verification,npk_nik_caching;} pairing;};
struct peer_svc_info {bool has_nik;uint8_t peer_nik[16];};
struct nan_paired_peer {bool valid;uint8_t peer_nmi[6],role,ndp_csid,nd_pmk[32];uint32_t lifetime_sec;int64_t established_us;};
void nan_app_remove_paired_peer(const uint8_t *peer);
static struct own_svc_info services[2];
static struct peer_svc_info peer;
static struct {wifi_nan_peer_creds_t peer_creds[2];unsigned num_peer_creds;struct nan_paired_peer paired_peers[2];} s_nan_ctx;
static uint8_t cache_services[2];
static int64_t native_clock=100;
static int64_t esp_timer_get_time(void){return native_clock;}
uint8_t esp32_mquickjs_wifi_nan_pairing_cache_service(unsigned slot){assert(slot<2);return cache_services[slot];}
bool esp32_mquickjs_wifi_nan_pairing_cache_bind(unsigned slot,uint8_t service){assert(slot<2);cache_services[slot]=service;return true;}
static bool s_nan_data_lock=true,wifi_task,locked,fail_alloc,peer_busy,fail_native;
static unsigned dispatches,allocations,native_calls,last_identity;
#define NAN_DATA_LOCK() do {assert(!locked);locked=true;}while(0)
#define NAN_DATA_UNLOCK() do {assert(locked);locked=false;}while(0)
static void forced_memzero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
bool current_task_is_wifi_task(void){return wifi_task;}
static struct own_svc_info *nan_find_own_svc(uint8_t id){assert(locked);return id>=1&&id<=2?&services[id-1]:NULL;}
static struct peer_svc_info *nan_find_peer_svc_exact(uint8_t own,uint8_t remote,const uint8_t*mac){
 assert(locked);return own>=1&&own<=2&&remote==9&&mac[0]==2?&peer:NULL;
}
static void *esp32_mquickjs_memory_wireless_calloc(const char*owner,size_t n,size_t size,int policy,int role){
 assert(wifi_task&&locked&&!strcmp(owner,"wifi.nan")&&n==1&&policy==0&&role==1);
 if(fail_alloc)return NULL;
 size_t*p=calloc(1,sizeof(size_t)+size);assert(p);*p=size;++allocations;return p+1;
}
static void esp32_mquickjs_memory_payload_free(void*ptr){
 assert(wifi_task&&!locked&&ptr&&allocations);size_t*p=(size_t*)ptr-1;
 for(size_t i=0;i<*p;i++){assert(!((uint8_t*)ptr)[i]);}
 --allocations;free(p);
}
static int eloop_register_timeout_blocking(int(*callback)(void*,void*),void*a,void*b){
 assert(!wifi_task&&!locked);++dispatches;wifi_task=true;int r=callback(a,b);wifi_task=false;return r;
}
static void *esp_nan_app_get_pasn_data(void){return NULL;}
static bool esp32_mquickjs_wifi_nan_ndp_peer_busy(const uint8_t*mac){assert(wifi_task&&mac);return peer_busy;}
