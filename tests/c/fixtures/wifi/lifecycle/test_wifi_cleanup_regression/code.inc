
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -33
#define portMAX_DELAY UINT32_MAX
static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { return fallback; }
#define ESP_ERR_INVALID_STATE -2
#define TAG "test"
#define ESP_LOGE(...) ((void)0)
#define IP_EVENT 1
#define WIFI_EVENT 2
#define ESP32QJS_WIFI_CONTROL_EVENT 3
#define WIFI_CONTROL_LINK_DRAINED 3
#define IP_EVENT_STA_GOT_IP 4
#define WIFI_EVENT_SCAN_DONE 5
#define WIFI_EVENT_STA_DISCONNECTED 6
#define WIFI_EVENT_STA_START 7
#define WIFI_EVENT_STA_CONNECTED 12
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t;
typedef struct { void *lock,*event_group,*scan_queue,*connect_queue,*driver_event_queue; } esp32_mquickjs_wifi_runtime_resources_t;
static struct {
    void *connect_timeout_timer,*ip_event_instance,*wifi_scan_event_instance,*control_event_instance;
    void *wifi_disconnect_event_instance,*wifi_connected_event_instance,*wifi_start_event_instance,*sta_netif;
    void *lock,*event_group,*scan_queue,*connect_queue,*driver_event_queue;
    atomic_uint callbacks_active;
    int radio_lease,cleanup_error,sta_detach_error;
    const char *cleanup_stage;
    bool cleanup_timer_stopped,cleanup_radio_released,initialized,runtime_cleanup_pending;
    uint32_t scan_generation,connect_generation,disconnect_epoch;bool disconnect_epoch_exhausted;
} s_wifi_state;
static struct { unsigned identity; } s_wifi_lifecycle;
static int boundary(int n);
static int esp32_mquickjs_wifi_radio_finish_lifecycle(void *t,bool shutdown) { (void)t;(void)shutdown;int err=boundary(10);if(!err)s_wifi_lifecycle.identity=0;return err; }
static int esp32_mquickjs_wifi_radio_quiesce_lifecycle(void *t) { (void)t;return boundary(9); }
static int calls[13], fail_at, freed, released;
static atomic_uint s_wifi_watch_control_mask;
static int s_wifi_runtime_resource_ops;
static int boundary(int n) { calls[n]++;return n==fail_at ? -n : ESP_OK; }
static void wifi_stop_connect_timeout_timer(void) { (void)boundary(1); }
static int esp_timer_stop_blocking(void *t,unsigned ticks) { assert(t && ticks);return boundary(1); }
static int esp_timer_delete(void *t) { assert(t);return boundary(2); }
static int esp_event_handler_instance_unregister(int base,int id,void *h) { (void)base;assert(h);return boundary(id); }
static void vTaskDelay(int n) { (void)n;assert(0); }
static int esp32_mquickjs_wifi_radio_release_and_stop_idle(int *lease) { int err=boundary(8);if(err)return err;*lease=0;released++;return 0; }
static int esp_wifi_clear_default_wifi_driver_and_handlers(void *n) { assert(n);return boundary(11); }
static void esp_netif_destroy(void *n) { assert(n);freed++; }
/* SDK-facing retirement boundary; its scheduler is covered separately by
 * test_wifi_netif_retirement using the complete production translation unit. */
static int esp32_mquickjs_wifi_netif_retire(void **n,int *error) {
    if(*error)return *error;
    if(!*n)return ESP_OK;
    *error=esp_wifi_clear_default_wifi_driver_and_handlers(*n);
    if(!*error){esp_netif_destroy(*n);*n=NULL;}
    return *error;
}
static void esp32_mquickjs_wifi_runtime_resources_deinit(esp32_mquickjs_wifi_runtime_resources_t *r,int *ops) {
    (void)ops;assert(r->lock && r->event_group && r->scan_queue && r->connect_queue && r->driver_event_queue);freed++;
}
