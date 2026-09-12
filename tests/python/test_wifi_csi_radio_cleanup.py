"""Deferred CSI production stop/close suffix tests with retained Radio failures."""
import unittest
from pathlib import Path
from test_wireless_control_regression import compile_run, function

ROOT = Path(__file__).resolve().parents[2]


class WiFiCsiRadioCleanup(unittest.TestCase):
    def test_failed_promiscuous_release_preserves_channel_queue_control_and_retry(self):
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c').read_text()
        code = PRELUDE + '\n'.join(function(source, name) for name in [
            'wifi_csi_radio_cleanup_failed', 'wifi_csi_finish_stop', 'wifi_csi_finish_close', 'wifi_csi_begin_stop'])
        compile_run(self, code + MAIN)


PRELUDE = r'''
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
'''

MAIN = r'''
int main(void) {
    esp32_mquickjs_event_queue_t queue=1;
    wifi_csi_session_t session={.radio_lease={true},.promiscuous_lease={true},
        .csi_enabled=true,.callback_registered=true,.event_queue=&queue,.event_queue_retained=true};
    atomic_init(&session.lifecycle,WIFI_CSI_RUNNING);session.resources=&session.storage;atomic_init(&session.resources->callbacks_active,0);
    fail_promiscuous=true;
    assert(wifi_csi_begin_stop(&session)==ESP_OK);wifi_csi_finish_stop(&session);wifi_csi_finish_close(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED && session.last_error==-31);
    assert(session.promiscuous_lease.acquired && session.radio_lease.acquired && !released_channel);
    assert(session.event_queue==&queue && session.event_queue_retained && !closed_queue && !released_queue && !destroyed);
    assert(disabled_csi==1 && unregistered_csi==1);
    fail_promiscuous=false;fail_radio=true;
    assert(wifi_csi_begin_stop(&session)==ESP_OK);wifi_csi_finish_stop(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_STOPPED && released_channel==1);
    wifi_csi_finish_close(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_FAULTED && session.radio_lease.acquired && !closed_queue && !destroyed);
    fail_radio=false;
    assert(wifi_csi_begin_stop(&session)==ESP_OK);wifi_csi_finish_stop(&session);wifi_csi_finish_close(&session);
    assert(atomic_load(&session.lifecycle)==WIFI_CSI_CLOSED && !session.radio_lease.acquired && !session.promiscuous_lease.acquired);
    assert(closed_queue==1 && released_queue==1 && destroyed==1 && disabled_csi==1 && unregistered_csi==1);
    wifi_csi_finish_close(&session);assert(closed_queue==1 && destroyed==1);
    /* A failed initial start can still have its initial STOPPED lifecycle but
     * own a partially enabled promiscuous lease: do not bypass cleanup. */
    session=(wifi_csi_session_t){.radio_lease={true},.promiscuous_lease={true}};
    atomic_init(&session.lifecycle,WIFI_CSI_STOPPED);session.resources=&session.storage;atomic_init(&session.resources->callbacks_active,0);
    assert(wifi_csi_begin_stop(&session)==ESP_OK && atomic_load(&session.lifecycle)==WIFI_CSI_STOPPING);
    wifi_csi_finish_stop(&session);assert(!session.promiscuous_lease.acquired);
}
'''
