
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define IRAM_ATTR
#define DRAM_ATTR
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x10c
#define ESP_ERR_WIFI_TWT_FULL 0x3017
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define WIFI_EVENT_ITWT_SETUP 28
#define WIFI_EVENT_ITWT_TEARDOWN 29
#define WIFI_EVENT_ITWT_PROBE 30
#define WIFI_EVENT_ITWT_SUSPEND 31
/* Information normalization is exercised by the probe-result fixture. */
static esp_err_t esp32_mquickjs_wifi_twt_information_post(const void *data,size_t size) {
    (void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
#define WIFI_EVENT_BTWT_SETUP 33
#define WIFI_EVENT_BTWT_TEARDOWN 34
static esp_err_t esp32_mquickjs_wifi_btwt_event_post(int id,const void *data,size_t size) {
    (void)id;(void)data;(void)size;assert(0);return ESP_ERR_INVALID_ARG;
}
#define WIFI_EVENT ((const char *)19)
#define ESP_EVENT_DECLARE_BASE(name)
#define ESP32QJS_WIFI_RADIO_CONTROL_EVENT ((const char *)20)
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked,allocations,live;
static bool allocation_error;
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=1;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked==1);locked=0;} while(0)
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps) {
    assert(!locked && n==8 && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));++allocations;
    if(allocation_error)return NULL;void *p=calloc(n,size);assert(p);++live;return p;
}
static void heap_caps_free(void *p) {assert(!locked);if(p){assert(live);--live;free(p);}}
static esp_err_t esp_event_post(const char *,int32_t,const void *,size_t,unsigned);
