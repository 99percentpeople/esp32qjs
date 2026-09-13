
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_PAIRING 1
#define ETH_ALEN 6
#define NAN_PASN_AUTH_TIMEOUT_SECS 10
#define ELOOP_ALL_CTX ((void*)-1)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 101
#define ESP_ERR_INVALID_ARG 102
#define ESP_ERR_INVALID_STATE 103
#define ESP_ERR_TIMEOUT 104
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define WIFI_IF_NAN 2
#define MSG_INFO 1
#define os_snprintf snprintf
#define os_memset memset
#define os_strlen strlen
#define os_snprintf_error(size,n) ((n)<0||(size_t)(n)>=(size))
typedef int esp_err_t;
enum nan_role {NAN_ROLE_IDLE,NAN_ROLE_PAIRING_INITIATOR,NAN_ROLE_PAIRING_RESPONDER};
typedef void(*esp_nan_pairing_key_installed_cb_t)(void);
struct pasn_data {int unused;};
struct rsn_pmksa_cache {int unused;};
static bool wifi_task;
static unsigned allocations,allocation_calls,fail_allocation,cache_alive,cache_calls,fail_cache,dispatches;
static int mac_error,pending_error,dispatch_error,protocol_error;
static bool tx_busy,unbound;
static void (*timer_cb)(void*,void*);
static void *timer_data,*timer_arg;
static unsigned timer_calls;
static bool fail_timer;
static int pasn_nd_pmk_global;
static void forced_memzero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
static void wpa_printf(int level,const char*fmt,...){(void)level;(void)fmt;}
bool current_task_is_wifi_task(void){return wifi_task;}
static bool is_zero_ether_addr(const uint8_t*p){return !memcmp(p,(uint8_t[6]){0},6);}
static void callback(void){}
static esp_nan_pairing_key_installed_cb_t esp_nan_pairing_get_key_installed_cb(void){return callback;}
