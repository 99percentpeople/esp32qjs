
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_SUPPORTED 0x106
typedef int esp_err_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool critical;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=false;}while(0)
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 0
static bool fail_allocation;
static unsigned native_allocations;
static size_t allocation_bytes;
static void*esp32_mquickjs_memory_wireless_calloc(const char*owner,size_t n,size_t bytes,int policy,int role){
 (void)owner;(void)policy;(void)role;assert(!critical&&n==1);
 if(fail_allocation)return NULL;assert(!native_allocations);native_allocations=1;allocation_bytes=bytes;return calloc(n,bytes);
}
static void esp32_mquickjs_memory_payload_free(void*p){
 assert(!critical&&native_allocations==1);
 for(size_t i=0;i<allocation_bytes;i++)assert(!((uint8_t*)p)[i]);
 native_allocations=0;free(p);
}
static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){memset(p,0,n);}
