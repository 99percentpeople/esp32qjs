
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
typedef enum { WIFI_CSI_STOPPED,WIFI_CSI_RUNNING,WIFI_CSI_STOPPING,WIFI_CSI_FAULTED,WIFI_CSI_CLOSED } wifi_csi_lifecycle_t;
typedef struct { bool acquired; } lease_t;
typedef int esp32_mquickjs_event_queue_t;
typedef struct { int cleanup_error;const char *cleanup_stage; } esp32_mquickjs_wifi_radio_status_t;
typedef struct {
    struct { atomic_uint callbacks_active; } storage, *resources;
    lease_t promiscuous_lease,radio_lease;
    bool csi_enabled,callback_registered,event_queue_retained;
    atomic_int lifecycle;
    int last_error;const char *last_error_stage;
    esp32_mquickjs_event_queue_t *event_queue;
} wifi_csi_session_t;
static bool fail_promiscuous,fail_radio;
static unsigned released_channel,closed_queue,released_queue,destroyed,disabled_csi,unregistered_csi;
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *s)
{ s->cleanup_error=-31;s->cleanup_stage="promiscuous-restore-enable";return ESP_OK; }
static void esp32_mquickjs_wifi_radio_release_promiscuous(lease_t *l) { if(!fail_promiscuous)l->acquired=false; }
static void esp32_mquickjs_wifi_radio_release_channel(lease_t *l) { assert(l->acquired);released_channel++; }
static void esp32_mquickjs_wifi_radio_release(lease_t *l) { if(!fail_radio)l->acquired=false; }
static int esp32_mquickjs_event_queue_discard_all(esp32_mquickjs_event_queue_t *q) { assert(q);return 0; }
static int esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *q) { assert(q);closed_queue++;return 0; }
static void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t *q) { assert(q);released_queue++; }
static void wifi_csi_maybe_destroy_resources(wifi_csi_session_t *s) { assert(!s->radio_lease.acquired && !s->promiscuous_lease.acquired);destroyed++; }
static void esp32_mquickjs_wifi_csi_resources_set_accepting(void *r,bool v) { assert(r && !v); }
static void esp32_mquickjs_wifi_csi_rx_native_enable(bool enabled){assert(!enabled);}
static int esp_wifi_set_csi(bool v) { assert(!v);disabled_csi++;return ESP_OK; }
static int esp_wifi_set_csi_rx_cb(void *cb,void *arg) { assert(!cb && !arg);unregistered_csi++;return ESP_OK; }
