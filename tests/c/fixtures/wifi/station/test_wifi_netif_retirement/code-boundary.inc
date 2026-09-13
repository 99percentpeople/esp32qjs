
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_EVENT_ANY_ID -1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_EVENT_DEFINE_BASE(name) static const char name[] = #name
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) do { assert(!*(p));*(p)=1; } while(0)
#define portEXIT_CRITICAL(p) do { assert(*(p));*(p)=0; } while(0)
typedef int esp_err_t,portMUX_TYPE,StaticSemaphore_t;
typedef int *SemaphoreHandle_t;
typedef unsigned TickType_t;
typedef void *esp_event_handler_instance_t;
typedef const char *esp_event_base_t;
typedef struct { bool alive;int stops,detaches,ips; } esp_netif_t;
static TickType_t ticks;
static int registrations,posts,register_error,post_error,detach_error;
static int callback_depth;
static bool auto_dispatch=true;
static void (*handler)(void *,esp_event_base_t,int32_t,void *);
static struct event { esp_event_base_t base;int id;uint32_t identity;esp_netif_t *netif; } queue[32];
static unsigned head,tail;
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s) { *s=0;return s; }
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait) { assert(!wait);if(*s)return 0;*s=1;return 1; }
static int xSemaphoreGive(SemaphoreHandle_t s) { assert(*s);*s=0;return 1; }
static TickType_t xTaskGetTickCount(void) { return ticks; }
static void dispatch(void) {
    if(head==tail)return;
    struct event e=queue[head++%32];callback_depth++;
    if(e.netif) { assert(e.netif->alive);e.netif->ips++; }
    else handler(NULL,e.base,e.id,&e.identity);
    callback_depth--;
}
static void vTaskDelay(unsigned n) { ticks+=n;if(auto_dispatch)dispatch(); }
static int esp_event_handler_instance_register(esp_event_base_t base,int id,
    void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg,void **out) {
    assert(base && id==ESP_EVENT_ANY_ID && !arg);registrations++;
    if(register_error)return register_error;
    handler=fn;*out=(void *)1;return ESP_OK;
}
static int esp_event_post(esp_event_base_t base,int id,const void *data,size_t len,unsigned wait) {
    assert(!wait && len==sizeof(uint32_t));posts++;
    if(post_error)return post_error;
    assert(tail-head<32);queue[tail++%32]=(struct event){base,id,*(const uint32_t *)data,NULL};return ESP_OK;
}
static void esp_netif_action_stop(esp_netif_t *n,esp_event_base_t base,int id,void *data) {
    (void)base;(void)id;assert(!data && callback_depth && n->alive);n->stops++;
    /* Stack STOP appends an IP event behind the currently executing job. */
    assert(tail-head<32);queue[tail++%32]=(struct event){.netif=n};
}
static int esp_wifi_clear_default_wifi_driver_and_handlers(esp_netif_t *n) {
    assert(callback_depth && n->alive && n->stops && !n->detaches);
    n->detaches++;return detach_error;
}
static void esp_netif_destroy(esp_netif_t *n) {
    assert(!callback_depth && n->alive && n->detaches==1 && n->ips==1);n->alive=false;
}
