
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define ESP32_MQUICKJS_SMARTCONFIG_TIMERS 9U
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_FINISHED 4
#define ESP_ERR_WIFI_NOT_INIT 5
#define ESP_ERR_TIMEOUT 6
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t, portMUX_TYPE;
typedef enum {WIFI_MODE_NULL,WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA} wifi_mode_t;
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {void *timer_arg;} ETSTimer;
typedef struct {uint64_t identity;uint32_t radio_generation;} esp32_mquickjs_wifi_smartconfig_token_t;
typedef struct {esp_err_t error;} esp32_mquickjs_wifi_smartconfig_timer_status_t;
typedef struct {bool enable_log,esp_touch_v2_enable_crypt;char *esp_touch_v2_key;} smartconfig_start_config_t;
typedef struct {uint8_t ssid[32],password[64];smartconfig_type_t type;uint8_t token,cellphone_ip[4];} smartconfig_event_got_ssid_pswd_t;
static int locked,allocs,frees,start_calls,stop_calls,fence_calls,scan_calls,rx_calls,list_calls;
static int native_status,ipc_error,start_error,stop_error,scan_error,rx_error,list_error,timer_error,drain_error;
static int native_allocation_error;
static bool oom_on_start;
static bool alloc_error,promiscuous,event_owned,event_closed,timer_owned,timer_closed,ack_busy;
static uint64_t ack_owner,ack_receipt;
static bool credential_ready;
static wifi_mode_t mode=WIFI_MODE_STA;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t s,int caps){assert(!locked && caps==3);if(alloc_error)return NULL;allocs++;return calloc(n,s);}
static void heap_caps_free(void *p){assert(!locked);frees++;free(p);}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
static esp_err_t esp_wifi_get_mode(wifi_mode_t *out){assert(!locked);*out=mode;return 0;}
static esp_err_t esp_wifi_get_promiscuous(bool *out){assert(!locked);*out=promiscuous;return 0;}
static esp_err_t esp_wifi_set_promiscuous(bool enabled){assert(!locked && !enabled);rx_calls++;if(!rx_error)promiscuous=false;return rx_error;}
static esp_err_t esp_wifi_scan_stop(void){assert(!locked);scan_calls++;return scan_error;}
static esp_err_t esp_wifi_clear_ap_list(void){assert(!locked);list_calls++;return list_error;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_begin(uint32_t gen,esp32_mquickjs_wifi_smartconfig_token_t *t){
 assert(!locked && !event_owned && !t->identity);event_owned=true;event_closed=false;native_allocation_error=0;t->identity=123;t->radio_generation=gen;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_status(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_events_status_t *out){
 assert(!locked && event_owned && t->identity==123);memset(out,0,sizeof(*out));out->token=*t;out->allocation_error=native_allocation_error;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_close(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && event_owned);event_closed=true;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_events_release(esp32_mquickjs_wifi_smartconfig_token_t *t){assert(event_closed && event_owned && !timer_owned && !ack_owner);event_owned=false;memset(t,0,sizeof(*t));return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_begin(const esp32_mquickjs_wifi_smartconfig_token_t *t,ETSTimer *const timers[9]){
 assert(!locked && event_owned && t->identity==123 && !timer_owned);for(int i=0;i<9;i++){assert(timers[i]);for(int j=0;j<i;j++)assert(timers[i]!=timers[j]);}
 timer_owned=true;timer_closed=false;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_status(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_timer_status_t *s){assert(t->identity==123 && timer_owned);s->error=timer_error;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_close(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && timer_owned);timer_closed=true;return 0;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_cleanup(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && timer_closed);return drain_error;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_timers_release(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && timer_closed && !native_status && !drain_error);timer_owned=false;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_reserve(uint64_t owner){if(ack_owner || ack_busy)return ESP_ERR_INVALID_STATE;ack_owner=owner;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_release(uint64_t owner){assert(owner==ack_owner && !ack_busy);ack_owner=0;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_start(uint64_t owner,smartconfig_type_t type,uint8_t token,const uint8_t *phone,uint64_t *receipt){assert(owner==ack_owner && type==SC_TYPE_ESPTOUCH && token==7 && phone[0]==192 && !*receipt);*receipt=ack_receipt=99;ack_busy=true;return 0;}
static esp_err_t esp32qjs_smartconfig_ack_stop(uint64_t owner,uint64_t receipt){assert(owner==ack_owner && receipt==99);return 0;}
static uint64_t esp32qjs_smartconfig_ack_status(bool *busy,bool *stopping,bool *completed,esp_err_t *error,esp_err_t *observation,int *socket_errno){(void)stopping;(void)completed;(void)error;(void)observation;(void)socket_errno;*busy=ack_busy;return ack_receipt;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_commit(const esp32_mquickjs_wifi_smartconfig_token_t *t){assert(t->identity==123 && credential_ready);credential_ready=false;return ESP_OK;}
static esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_copy(const esp32_mquickjs_wifi_smartconfig_token_t *t,esp32_mquickjs_wifi_smartconfig_credentials_t *c){assert(t->identity==123);if(!credential_ready)return ESP_ERR_NOT_FINISHED;memset(c,0x55,sizeof(*c));c->network.type=SC_TYPE_ESPTOUCH;c->network.token=7;c->network.cellphone_ip[0]=192;return 0;}
