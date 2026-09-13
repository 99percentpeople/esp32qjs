
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_INVALID_SIZE 4
#define ESP_ERR_TIMEOUT 5
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int esp_err_t,portMUX_TYPE;
typedef uint32_t TickType_t;
typedef const char *esp_event_base_t;
static const char base_name[]="SC_EVENT";
#define SC_EVENT base_name
enum {SC_EVENT_SCAN_DONE,SC_EVENT_FOUND_CHANNEL,SC_EVENT_GOT_SSID_PSWD,SC_EVENT_SEND_ACK_DONE};
typedef enum {SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS,SC_TYPE_ESPTOUCH_V2} smartconfig_type_t;
typedef struct {
    uint8_t ssid[32],password[64];bool bssid_set;uint8_t bssid[6];
    smartconfig_type_t type;uint8_t token,cellphone_ip[4];
} smartconfig_event_got_ssid_pswd_t;
static int locked,allocations,frees,posts;
static bool fail_alloc;
static size_t allocation_size;
static const void *forwarded_data;
static esp_event_base_t forwarded_base;
static size_t forwarded_length;
static int32_t forwarded_event;
static TickType_t forwarded_wait;
static uint8_t sdk_config[0xb08];
uint8_t *g_config_data=sdk_config;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!locked && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(fail_alloc)return NULL;allocations++;allocation_size=n*size;return calloc(n,size);
}
static void heap_caps_free(void *p) {
    assert(!locked && p);for(size_t i=0;i<allocation_size;i++)assert(((uint8_t *)p)[i]==0);
    frees++;free(p);
}
static void esp32_mquickjs_wireless_secure_zero(void *data,size_t size) {
    volatile uint8_t *p=data;while(size--)*p++=0;
}
esp_err_t __real_esp_event_post(esp_event_base_t base,int32_t event,const void *data,size_t len,TickType_t wait) {
    assert(!locked);posts++;forwarded_base=base;forwarded_event=event;
    forwarded_data=data;forwarded_length=len;forwarded_wait=wait;return ESP_ERR_TIMEOUT;
}
