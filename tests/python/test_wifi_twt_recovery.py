"""Deferred production TWT recovery admission/checkpoint/phase routing.

Uses actual Radio lease registry, TWT owners and close functions. SDK cancellation,
configuration snapshots and physical stop/shutdown are injected call boundaries;
real STOP/deinit/event scheduling and runtime replay remain separate stage gates.
Do not import, compile or execute during Wi-Fi API implementation.
"""
import re
import unittest
from test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from test_wifi_twt_radio import SOURCE, INTERNAL
from test_wifi_config_controls import structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def recovery_code():
    source = SOURCE.read_text()
    code = agreement_radio_code()
    code = code.replace('bool uncertain;} s_interval;', 'bool uncertain,restore_pending;} s_interval;')
    code = code.replace('struct {unsigned identity,generation;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('uint32_t next_operation_identity;', 'uint32_t next_operation_identity;bool stop_submitted;unsigned event_phase;')
    header = (INTERNAL / 'esp32_mquickjs_wifi_radio.h').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_recovery_kind_t;', header).group(0)
    code += structure(header, 'esp32_mquickjs_wifi_recovery_request_t')
    code += re.search(r'static struct \{[^}]*\} s_twt_recovery;', source).group(0)
    code += BOUNDARIES
    for name in ('wifi_radio_twt_recovery_exact_locked', 'wifi_radio_twt_managed_lease_locked',
                 'wifi_radio_twt_recovery_owners_locked', 'wifi_radio_twt_recovery_active',
                 'wifi_radio_twt_recovery_begin', 'wifi_radio_twt_recovery_prepare',
                 'wifi_radio_twt_recovery_checkpoint', 'wifi_radio_twt_recovery_phase',
                 'wifi_radio_twt_recovery_stopped', 'wifi_radio_twt_recovery_finish'):
        code += extract(source, name)
    return code + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]


class WiFiTwtRecovery(unittest.TestCase):
    def test_foreign_owner_stale_generation_group_freeze_and_original_owner_retirement(self):
        compile_run(self, recovery_code() + MAIN)


BOUNDARIES = r'''
#define RADIO_EVENTS_IDLE 0
#define WIFI_RADIO_EAP_PENDING eap_pending
static bool eap_pending;
static struct {void *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static unsigned cancelled,prepared,captured,stop_calls,shutdown_calls;
static int cancel_error,prepare_error,capture_error,physical_error;
static esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel(uint32_t id) {
    assert(locks && !critical && id==s_twt_probe.state.native_identity);++cancelled;return cancel_error;
}
static int wifi_radio_policy_restart_prepare_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    assert(locks && !critical && t->identity==s_radio.lifecycle.identity && mode==s_radio.effective_mode);
    ++prepared;return prepare_error;
}
static int wifi_radio_restart_configs_capture_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    assert(locks && !critical && t->identity==s_radio.lifecycle.identity && mode==s_radio.effective_mode);
    ++captured;return capture_error;
}
static int wifi_radio_restart_configs_record(const char *stage,int error,bool mutated) {
    assert(locks && !critical && stage && !mutated);return error;
}
static int wifi_radio_stop_lease_locked(const esp32_mquickjs_wifi_radio_lease_t *owner,bool twt) {
    assert(locks && !critical && !owner && twt);++stop_calls;return physical_error;
}
static int wifi_radio_shutdown_lease_locked(const esp32_mquickjs_wifi_radio_lease_t *owner,bool twt) {
    assert(locks && !critical && !owner && twt);++shutdown_calls;return physical_error;
}
'''

MAIN = r'''
static bool owners_allowed(void) {
    wifi_radio_operation_lock();bool allowed=wifi_radio_twt_recovery_owners_locked();wifi_radio_operation_unlock();return allowed;
}
int main(void) {
    individual_reset();s_radio.stop_required=true;
    esp32_mquickjs_wifi_itwt_options_t cfg={.config={.setup_cmd=TWT_REQUEST,.min_wake_dura=1,
        .wake_invl_mant=10256,.timeout_time_ms=100},.timeout_ms=6000};
    esp32_mquickjs_wifi_btwt_options_t bc={.config={.setup_cmd=TWT_REQUEST,.btwt_id=1,
        .timeout_time_ms=5000},.timeout_ms=6000};
    esp32_mquickjs_wifi_twt_token_t a={0},b={0},c={0},rejected={0};
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&cfg,&a));
    assert(!esp32_mquickjs_wifi_radio_twt_individual_submit(&cfg,&b));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_submit(&bc,&c));
    esp32_mquickjs_wifi_radio_lease_t app={0},foreign={0};
    wifi_radio_operation_lock();
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&app));
    assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR,WIFI_MODE_STA,&foreign));
    wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_recovery_request_t request={s_radio.generation,0,ESP32_MQUICKJS_WIFI_RECOVERY_TWT};
    esp32_mquickjs_wifi_radio_lifecycle_t life={0};wifi_mode_t mode;
    assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_STATE);
    assert(!life.identity && !s_twt_individual[0].closing && !s_twt_broadcast[1]->closing);
    wifi_radio_operation_lock();wifi_radio_release_locked(&foreign);wifi_radio_operation_unlock();
    ++request.generation;assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_STATE);--request.generation;
    request.identity=1;assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_ARG);request.identity=0;
    eap_pending=true;assert(wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode)==ESP_ERR_INVALID_STATE);eap_pending=false;
    assert(!wifi_radio_twt_recovery_begin(&app,NULL,NULL,&request,&life,&mode));
    assert(mode==WIFI_MODE_STA && life.identity && individual_owners()==3);
    assert(s_twt_individual[0].closing && s_twt_individual[1].closing && s_twt_broadcast[1]->closing);
    assert(esp32_mquickjs_wifi_radio_twt_individual_submit(&cfg,&rejected)==ESP_ERR_INVALID_STATE && !rejected.identity);
    assert(!owners_allowed()); /* helper has not transferred yet */
    wifi_radio_operation_lock();wifi_radio_release_locked(&app);wifi_radio_operation_unlock();
    assert(owners_allowed());
    esp32_mquickjs_wifi_radio_lifecycle_t stale=life;++stale.identity;
    assert(wifi_radio_twt_recovery_prepare(&stale)==ESP_ERR_INVALID_STATE);
    assert(!wifi_radio_twt_recovery_prepare(&life) && !cancelled);
    assert(!wifi_radio_twt_recovery_checkpoint(&life,mode) && prepared==1 && captured==1);
    assert(wifi_radio_twt_recovery_phase(&stale,false)==ESP_ERR_INVALID_STATE && !stop_calls);
    physical_error=77;assert(wifi_radio_twt_recovery_phase(&life,false)==77 && stop_calls==1);
    physical_error=0;assert(!wifi_radio_twt_recovery_phase(&life,false));
    /* SDK STOP observation boundary, without fabricating owner retirement. */
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;s_twt_recovery.stopped=true;
    assert(!wifi_radio_twt_recovery_stopped(&life));
    assert(wifi_radio_twt_recovery_finish(&life)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&a));
    assert(!esp32_mquickjs_wifi_radio_twt_individual_close(&b));
    assert(!esp32_mquickjs_wifi_radio_twt_broadcast_close(&c));
    assert(!individual_owners() && !wifi_radio_twt_recovery_phase(&life,true) && shutdown_calls==1);
    s_radio.driver_owned=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;++s_radio.generation;
    assert(!wifi_radio_twt_recovery_finish(&life) && !s_twt_recovery.lifecycle.identity);
    assert(s_radio.lifecycle.identity==life.identity); /* replay still owns lifecycle */
    /* Live probe cancellation is a required, retryable pre-disconnect prefix. */
    memset(&s_radio.lifecycle,0,sizeof(s_radio.lifecycle));s_radio.driver_owned=s_radio.started=s_radio.stop_required=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    esp32_mquickjs_wifi_twt_token_t probe={0};assert(!esp32_mquickjs_wifi_radio_twt_probe_submit(100,&probe));
    request.generation=s_radio.generation;life=(esp32_mquickjs_wifi_radio_lifecycle_t){0};
    assert(!wifi_radio_twt_recovery_begin(NULL,NULL,NULL,&request,&life,&mode));
    assert(s_twt_probe.state.cleanup_pending && individual_owners()==1);
    cancel_error=78;assert(wifi_radio_twt_recovery_prepare(&life)==78 && !s_twt_recovery.prepared && cancelled==1);
    cancel_error=0;assert(!wifi_radio_twt_recovery_prepare(&life) && s_twt_recovery.prepared && cancelled==2);
    assert(!wifi_radio_twt_recovery_prepare(&life) && cancelled==2);
    assert(!esp32_mquickjs_wifi_radio_twt_probe_retire(&probe));
    assert(!individual_owners() && !critical && !locks);
    free(s_twt_individual);free(s_twt_broadcast);return 0;
}
'''
