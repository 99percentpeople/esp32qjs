
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1
#define CONFIG_LWIP_IPV4 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 0
#define CONFIG_IDF_TARGET_ESP32C5 0
#define IRAM_ATTR
#define DRAM_ATTR
#define portMUX_INITIALIZER_UNLOCKED 0
#define ESP_TIMER_TASK 0
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FINISHED 5
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
typedef int esp_err_t,portMUX_TYPE;
typedef void ETSTimerFunc(void *);
typedef struct { void *timer_arg;uint32_t timer_expire; } ETSTimer;
typedef struct {uint64_t identity;uint32_t radio_generation;} esp32_mquickjs_wifi_smartconfig_token_t;
typedef struct {void (*callback)(void *);void *arg;int dispatch_method;const char *name;} esp_timer_create_args_t;
typedef struct timer {esp_timer_create_args_t args;bool alive,armed;} *esp_timer_handle_t;
static struct timer timer_storage[64];
static int locked,allocs,frees,creates,starts,stops,deletes,forwards;
static int create_error,start_error,stop_error,blocking_error,delete_error;
static bool alloc_error;
static uint64_t latest_us;
static bool latest_repeat;
static void (*during_cleanup)(void);
#define portENTER_CRITICAL_SAFE(p) do{(void)(p);assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL_SAFE(p) do{(void)(p);assert(locked);locked=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!locked && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(alloc_error)return NULL;
    allocs++;return calloc(n,size);
}
static void heap_caps_free(void *p){assert(!locked && p);frees++;free(p);}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(!locked && args->callback && args->arg && args->dispatch_method==ESP_TIMER_TASK);
    if(create_error)return create_error;assert(creates<64);
    *out=&timer_storage[creates++];(*out)->args=*args;(*out)->alive=true;return ESP_OK;
}
static esp_err_t esp_timer_stop(esp_timer_handle_t t) {
    assert(!locked && t && t->alive);stops++;
    if(stop_error)return stop_error;bool was=t->armed;t->armed=false;return was?ESP_OK:ESP_ERR_INVALID_STATE;
}
static esp_err_t esp_timer_stop_blocking(esp_timer_handle_t t,unsigned ticks) {
    assert(!locked && t && t->alive && ticks==1);stops++;
    if(during_cleanup)during_cleanup();
    if(blocking_error)return blocking_error;t->armed=false;return ESP_OK;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t t) {
    assert(!locked && t && t->alive);deletes++;
    if(delete_error)return delete_error;assert(!t->armed);t->alive=false;return ESP_OK;
}
static esp_err_t start(esp_timer_handle_t t,uint64_t us,bool repeat) {
    assert(!locked && t && t->alive && !t->armed);starts++;latest_us=us;latest_repeat=repeat;
    if(start_error)return start_error;t->armed=true;return ESP_OK;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t t,uint64_t us){return start(t,us,false);}
static esp_err_t esp_timer_start_periodic(esp_timer_handle_t t,uint64_t us){return start(t,us,true);}
void __real_ets_timer_setfn(ETSTimer *t,ETSTimerFunc *fn,void *arg){assert(!locked && t && fn);(void)arg;forwards++;}
void __real_ets_timer_disarm(ETSTimer *t){assert(!locked && t);forwards++;}
void __real_ets_timer_done(ETSTimer *t){assert(!locked && t);forwards++;}
void __real_ets_timer_arm_us(ETSTimer *t,uint32_t us,bool repeat){assert(!locked && t && us);(void)repeat;forwards++;}
void __real_ets_timer_arm(ETSTimer *t,uint32_t ms,bool repeat){assert(!locked && t && ms);(void)repeat;forwards++;}
