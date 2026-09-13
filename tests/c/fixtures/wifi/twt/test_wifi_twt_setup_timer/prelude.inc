
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define IRAM_ATTR
#define DRAM_ATTR
#define _Static_assert(...)
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x10c
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ESP_TIMER_TASK 0
typedef int esp_err_t;
typedef unsigned portMUX_TYPE;
typedef struct __attribute__((packed)) {uint32_t timer_expire,pad[2];void *timer_arg;} ETSTimer;
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
#define portENTER_CRITICAL_SAFE(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL_SAFE(p) do{(void)(p);assert(locked);locked=0;}while(0)
static bool allocation_error;
static unsigned allocations, native_live;
static void *heap_caps_calloc(size_t count,size_t bytes,int caps) {
    assert(!locked && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));++allocations;
    if(allocation_error)return NULL;void *p=calloc(count,bytes);assert(p);++native_live;return p;
}
static void heap_caps_free(void *p) {assert(!locked);if(p){assert(native_live);--native_live;free(p);}}
esp_err_t esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
esp_err_t esp_timer_stop(esp_timer_handle_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
esp_err_t esp_timer_start_once(esp_timer_handle_t,uint64_t);
