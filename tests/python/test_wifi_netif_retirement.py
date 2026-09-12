"""Controlled default-loop scheduling against the entire production retire unit.

No replacement retirement state machine: only SDK queue/netif/RTOS boundaries
are simulated. Execution is deferred to the Wi-Fi API phase test run.
"""
import pathlib
import unittest
from test_wireless_control_regression import compile_run

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_netif.c'


class WiFiNetifRetirement(unittest.TestCase):
    def code(self, wait_source=None):
        source = '\n'.join(line for line in SOURCE.read_text().splitlines()
                           if not line.startswith('#include '))
        boundary = r'''
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
'''
        if wait_source is None:
            wait_source = 'static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { return fallback; }\n'
        return boundary + wait_source + source

    def test_queue_timeout_late_delivery_suffix_and_detach_poison(self):
        compile_run(self, self.code() + r'''
int main(void) {
    esp_netif_t sta={.alive=true},ap={.alive=true};
    esp_netif_t *s=&sta,*a=&ap;int se=0,ae=0;
    assert(esp32_mquickjs_wifi_netif_retire(NULL,&se)==ESP_ERR_INVALID_ARG);
    register_error=17;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==17 && s && !se && !posts);
    register_error=0;post_error=19;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==19 && s && !se && !sta.stops);
    assert(s_retire.phase==NETIF_IDLE && !s_retire.netif);
    post_error=0;auto_dispatch=false;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT);
    uint32_t first=s_retire.identity;int accepted=posts;
    assert(s && !se && s_retire.phase==NETIF_DETACH_QUEUED && !sta.detaches);
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_ERR_INVALID_STATE);
    assert(a && !ae && !ap.stops && posts==accepted);
    /* Retry never re-posts a job already admitted, even across runtime detach. */
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT && posts==accepted);
    dispatch();assert(sta.detaches==1 && !sta.ips && s_retire.phase==NETIF_DETACHED);
    post_error=23;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==23 && s && !se);
    assert(s_retire.phase==NETIF_DETACHED && sta.detaches==1);
    post_error=0;
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_ERR_TIMEOUT);
    assert(s_retire.phase==NETIF_FENCE_QUEUED && sta.alive);
    dispatch();assert(sta.ips==1 && sta.alive);
    dispatch();assert(s_retire.phase==NETIF_DONE && sta.alive);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_OK && !s && !sta.alive);
    assert(esp32_mquickjs_wifi_netif_retire(&s,&se)==ESP_OK);
    /* A duplicate/stale completion cannot detach the next slot occupant. */
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_ERR_TIMEOUT);
    handler(NULL,ESP32QJS_WIFI_NETIF_EVENT,NETIF_DETACH_QUEUED,&first);
    handler(NULL,ESP32QJS_WIFI_NETIF_EVENT,NETIF_FENCE_QUEUED,&first);
    assert(!ap.detaches && s_retire.phase==NETIF_DETACH_QUEUED);
    auto_dispatch=true;
    assert(esp32_mquickjs_wifi_netif_retire(&a,&ae)==ESP_OK && !a && !ae && !ap.alive);
    assert(sta.detaches==1 && ap.detaches==1 && registrations==2);
    /* SDK error is different from admission/timeout: permanently poison owner. */
    esp_netif_t failed={.alive=true};esp_netif_t *f=&failed;int fe=0;
    detach_error=29;
    assert(esp32_mquickjs_wifi_netif_retire(&f,&fe)==29 && fe==29 && f && failed.alive);
    accepted=posts;detach_error=0;
    assert(esp32_mquickjs_wifi_netif_retire(&f,&fe)==29 && posts==accepted && failed.detaches==1);
    while(head!=tail)dispatch();
    /* Identity exhaustion is explicit and never wraps/reuses an old event. */
    esp_netif_t last={.alive=true};esp_netif_t *l=&last;int le=0;
    s_retire.identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_netif_retire(&l,&le)==ESP_ERR_INVALID_STATE);
    assert(l && !le && !last.detaches && posts==accepted);
    return 0;
}
''')
