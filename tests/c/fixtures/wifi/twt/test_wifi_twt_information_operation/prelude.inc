
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x104
#define WIFI_EVENT_ITWT_SUSPEND 31
#define WIFI_EVENT ((const char *)19)
typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool locked,fail_allocate;
static unsigned allocations,posts;
static esp_err_t post_error,tx_error,timer_error;
static TaskHandle_t task=(void *)31;
static uint32_t current_identity;
static bool inspect_post;
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=true;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked);locked=false;} while(0)
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return task;}
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps){
    assert(!locked && caps==3);++allocations;return fail_allocate?NULL:calloc(n,size);
}
typedef struct {esp_err_t status;uint8_t flow_id_bitmap;uint32_t actual_suspend_time_ms[8];} wifi_event_sta_itwt_suspend_t;
