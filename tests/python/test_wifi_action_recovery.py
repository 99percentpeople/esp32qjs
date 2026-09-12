"""Deferred production Action/ROC physical recovery phases and lease handoff.

The real Radio registry, admission, stop/shutdown, Action ledger and retirement
are composed unchanged. SDK stop/deinit, callback draining, unrelated brokers and
helper/configuration storage are injected boundaries. No fixture import/compile/
execution during API implementation; native SDK and helper integration remain
separate stage gates.
"""
import unittest
from test_wifi_action_radio import radio_code, MAIN as RADIO_MAIN
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def recovery_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = radio_code(profile, ap)
    code = code.replace('bool uncertain;} s_interval;', 'bool uncertain,restore_pending;} s_interval;')
    code = code.replace('struct {unsigned identity,generation;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('bool event_fence_posted,event_fence_seen;', '''bool event_fence_posted,event_fence_seen;
        bool stop_submitted,ap_reopen_pending,ap_reopen_attempted,ap_reopen_quiesced,ap_reopen_restore_complete;
        wifi_mode_t event_live;unsigned ap_stop_phase;
        unsigned ap_transition_application,ap_transition_station,ap_transition_access_point;
        esp_err_t ap_stop_error,cleanup_error,channel_observation_error;
        uint64_t channel_observation_revision;uint8_t primary_channel;wifi_second_chan_t secondary_channel;''')
    code += BOUNDARIES
    for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
                 'wifi_radio_action_recovery_exact_locked',
                 'esp32_mquickjs_wifi_radio_action_recovery_active',
                 'esp32_mquickjs_wifi_radio_check_stopped_action_recovery',
                 'wifi_radio_action_recovery_capture_owner_locked', 'esp32_mquickjs_wifi_radio_begin_action_recovery',
                 'wifi_radio_stop_owners_locked', 'wifi_radio_stop_lease_locked', 'wifi_radio_stop_locked',
                 'wifi_radio_shutdown_lease_locked', 'wifi_radio_shutdown_locked',
                 'wifi_radio_action_recovery_phase', 'esp32_mquickjs_wifi_radio_stop_action_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_action_recovery',
                 'esp32_mquickjs_wifi_radio_finish_action_recovery',
                 'esp32_mquickjs_wifi_radio_finish_lifecycle'):
        code += extract(source, name)
    return code + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]


class WiFiActionRecovery(unittest.TestCase):
    def test_exact_admission_physical_proof_owner_consumption_and_cleanup_suffix(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, recovery_code(profile, ap) + MAIN)


BOUNDARIES = r'''
#include <stdatomic.h>
#define RADIO_EVENTS_STOP 2
#define AP_STOP_IDLE 0
#define WIFI_EVENT 4
#define ESP_EVENT_ANY_ID -1
#define ESP_ERR_WIFI_NOT_STARTED 101
#define portMAX_DELAY UINT32_MAX
/* Raw TX recovery is outside this fixture; exact guards cannot grant it. */
static struct {struct {unsigned generation,identity,radio_lease_identity;} operation;bool stopped,sdk_fenced;} s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;return false;}
static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t *lease) {(void)lease;return false;}
static int esp32_mquickjs_wifi_raw_tx_broker_quiesce(unsigned generation,const void *token) {(void)generation;(void)token;assert(!"unexpected Raw TX recovery");return -1;}
static struct {void *snapshot;} s_config_restart;
static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static struct {uint32_t generation;} s_inactive_history;
static struct {uint32_t generation;} s_scan_parameters;
static unsigned s_tx_rates,s_policies;
static void *s_channel_event_instance;
static atomic_uint s_channel_callbacks;
static unsigned stop_calls,stop_waits,deinit_calls,unregister_calls;
static int stop_error,wait_error,deinit_error,unregister_error;
static bool wait_expired;
typedef struct {bool unregister_written;} esp32_mquickjs_wifi_vendor_ie_broker_status_t;
typedef struct {uint32_t generation;} esp32_mquickjs_wifi_raw_tx_broker_status_t;
static void wifi_radio_set_state(esp32_mquickjs_wifi_radio_driver_state_t value) {assert(locks && !critical);s_radio.driver_state=value;}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t mode) {(void)mode;assert(locks && !critical);s_radio.event_phase=phase;return 0;}
static void wifi_radio_capture_stop_snapshot_locked(void) {assert(locks && !critical);}
static int wifi_radio_wait_events(void) {assert(locks && !critical);++stop_waits;if(!wait_error)s_radio.event_phase=RADIO_EVENTS_IDLE;return wait_error;}
static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned ticks) {assert(locks && !critical);return wait_expired?0:ticks;}
static void vTaskDelay(unsigned ticks) {assert(ticks==1 && locks && !critical);atomic_store(&s_channel_callbacks,0);}
static int esp_wifi_stop(void) {assert(locks && !critical && s_action.lease.acquired);++stop_calls;return stop_error;}
#undef esp_wifi_deinit
static int esp_wifi_deinit(void) {
    assert(locks && !critical && !s_radio.stop_required && !s_channel_event_instance);
    assert(s_action.lease.acquired && s_radio.operation.identity);++deinit_calls;return deinit_error;
}
#define esp_wifi_deinit(...) WIFI_RADIO_MUTATION(esp_wifi_deinit(__VA_ARGS__))
static int esp_event_handler_instance_unregister(int base,int id,void *instance) {
    assert(locks && !critical && base==WIFI_EVENT && id==ESP_EVENT_ANY_ID && instance);
    ++unregister_calls;return unregister_error;
}
static int esp32_mquickjs_wifi_vendor_ie_broker_unregister(unsigned generation) {assert(locks && !critical && generation);return 0;}
static void esp32_mquickjs_wifi_vendor_ie_broker_status(esp32_mquickjs_wifi_vendor_ie_broker_status_t *out) {*out=(esp32_mquickjs_wifi_vendor_ie_broker_status_t){0};}
static int esp32_mquickjs_wifi_vendor_ie_broker_reset(unsigned generation) {assert(locks && !critical && generation);return 0;}
static void esp32_mquickjs_wifi_raw_tx_broker_status(esp32_mquickjs_wifi_raw_tx_broker_status_t *out) {*out=(esp32_mquickjs_wifi_raw_tx_broker_status_t){0};}
static int esp32_mquickjs_wifi_raw_tx_broker_unregister(unsigned generation) {(void)generation;assert(!"unexpected Raw TX ownership");return 0;}
static bool esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(unsigned generation) {(void)generation;assert(!"unexpected Raw TX generation");return false;}
static int esp32_mquickjs_wifi_interval_invalidate(void *storage,unsigned generation) {assert(storage==&s_interval && generation && locks && !critical);return 0;}
static void esp32_mquickjs_wifi_tx_rate_invalidate(void *storage) {assert(storage==&s_tx_rates && locks && !critical);}
static void esp32_mquickjs_wifi_policy_invalidate(void *storage) {assert(storage==&s_policies && locks && !critical);}
static void wifi_radio_restart_configs_discard_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {assert(token && !s_config_restart.snapshot && locks && !critical);}
'''

MAIN = r'''
static wifi_action_tx_req_t *request;
static size_t request_bytes;
static esp32_mquickjs_wifi_action_token_t operation;
static esp32_mquickjs_wifi_radio_lifecycle_t recovery;
static void setup_recovery(void) {
    reset_action();memset(&operation,0,sizeof(operation));memset(&recovery,0,sizeof(recovery));
    request->op_id=0; /* Each independent SDK submission starts unassigned. */
    memset(&s_config_restart,0,sizeof(s_config_restart));memset(&s_policy_restart,0,sizeof(s_policy_restart));
    s_radio.stop_required=true;s_radio.event_live=WIFI_MODE_STA;s_channel_event_instance=&s_channel_callbacks;
    atomic_store(&s_channel_callbacks,0);stop_calls=stop_waits=deinit_calls=unregister_calls=0;
    stop_error=wait_error=deinit_error=unregister_error=0;wait_expired=false;
    assert(esp32_mquickjs_wifi_radio_action_send(request,request_bytes,&operation)==ESP_OK);
}
static int admit(const esp32_mquickjs_wifi_radio_lease_t *application) {
    wifi_mode_t mode=(wifi_mode_t)99;
    int error=esp32_mquickjs_wifi_radio_begin_action_recovery(application,NULL,NULL,&operation,&recovery,&mode);
    assert(mode==(error==ESP_OK?WIFI_MODE_STA:WIFI_MODE_NULL));
    assert(!stop_calls && !deinit_calls && !unregister_calls && action_owners()==1);
    return error;
}
static void late_owner(void) {s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=500;}
int main(void) {
    request_bytes=sizeof(*request)+1;request=calloc(1,request_bytes);assert(request);
    *request=(wifi_action_tx_req_t){.ifx=WIFI_IF_STA,.type=WIFI_OFFCHAN_TX_REQ,.channel=6,
        .wait_time_ms=100,.rx_cb=esp32_mquickjs_wifi_action_receive,.data_len=1};
    for(unsigned slot=1;slot<WIFI_RADIO_MAX_LEASES;++slot) {
        setup_recovery();s_radio.leases[slot].identity=500;
        assert(admit(NULL)==ESP_ERR_INVALID_STATE && !recovery.identity && operation.identity);
    }
    setup_recovery();on_lock=late_owner;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();++operation.identity;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();++operation.generation;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_radio.wake_locks=1;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_interval.restore_pending=true;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_tx_rate_lease.restore_pending=true;assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    setup_recovery();s_radio.next_lifecycle_identity=0;assert(admit(NULL)==ESP_ERR_NO_MEM);
    setup_recovery();s_radio.next_lifecycle_identity=UINT32_MAX;assert(admit(NULL)==ESP_OK);
    assert(recovery.identity==UINT32_MAX && !s_radio.next_lifecycle_identity);

    setup_recovery();esp32_mquickjs_wifi_radio_lease_t application={0};
    wifi_radio_operation_lock();
    assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&application)==0);
    wifi_radio_operation_unlock();
    assert(admit(NULL)==ESP_ERR_INVALID_STATE);
    assert(admit(&application)==ESP_OK && application.acquired && operation.identity);
    wifi_radio_operation_lock();
    assert(wifi_radio_action_recovery_capture_owner_locked(&recovery)==s_action.lease.identity);
    s_action.lane.cancel_busy=true;assert(!wifi_radio_action_recovery_capture_owner_locked(&recovery));
    s_action.lane.cancel_busy=false;
    wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==ESP_ERR_INVALID_STATE && !stop_calls);
    wifi_radio_operation_lock();wifi_radio_release_locked(&application);wifi_radio_operation_unlock();
    esp32_mquickjs_wifi_radio_lifecycle_t stale=recovery;++stale.identity;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&stale)==ESP_ERR_INVALID_STATE && !stop_calls);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_action_recovery_active(&recovery));
    assert(!esp32_mquickjs_wifi_radio_action_recovery_active(&stale));
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    stop_error=71;assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==71 && stop_calls==1);
    stop_error=0;wait_error=72;
    assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==72 && stop_calls==2 && s_radio.stop_submitted);
    wait_error=0;assert(esp32_mquickjs_wifi_radio_stop_action_recovery(&recovery)==0 && stop_calls==2);
    assert(!s_action.lane.physical_termination && !s_radio.started && operation.identity);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&stale)==ESP_ERR_INVALID_STATE);
    s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=500;
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    s_radio.leases[WIFI_RADIO_MAX_LEASES-1].identity=0;
    s_action.lane.cancel_busy=true;
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    s_action.lane.cancel_busy=false;
    unregister_error=73;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==73 && !deinit_calls);
    unregister_error=0;wait_expired=true;atomic_store(&s_channel_callbacks,1);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==ESP_ERR_TIMEOUT && !deinit_calls);
    assert(!s_channel_event_instance && unregister_calls==2 && !s_action.lane.physical_termination);
    atomic_store(&s_channel_callbacks,0);wait_expired=false;deinit_error=74;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==74 && deinit_calls==1);
    assert(s_radio.driver_owned && !s_action.lane.physical_termination && s_radio.generation==7);
    deinit_error=0;
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==ESP_ERR_TIMEOUT && deinit_calls==2);
    assert(!s_radio.driver_owned && s_action.lane.physical_termination && !s_action.lane.terminal);
    assert(operation.identity && action_owners()==1 && s_radio.generation==7);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==ESP_ERR_TIMEOUT && deinit_calls==2);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_OK);
    unsigned queries=quiescent_calls,cancels=cancel_calls,fences=fence_calls,markers=posts;
    assert(esp32_mquickjs_wifi_radio_action_cancel(&operation)==0 && cancel_calls==cancels);
    esp32_mquickjs_wifi_action_lane_t result;
    assert(esp32_mquickjs_wifi_radio_action_retire(&operation,&result)==ESP_OK);
    assert(result.physical_termination && !result.terminal && result.terminal_status==-1);
    assert(!operation.identity && !action_owners() && s_radio.generation==7);
    assert(quiescent_calls==queries && fence_calls==fences && posts==markers);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==0);
    assert(s_radio.generation==8 && deinit_calls==2 && stop_calls==2 && unregister_calls==2);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&recovery)==0 && s_radio.generation==8);
    assert(esp32_mquickjs_wifi_radio_check_stopped_action_recovery(&recovery)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_OK);
    assert(!esp32_mquickjs_wifi_radio_action_recovery_active(&recovery));
    assert(s_radio.lifecycle.identity==recovery.identity && s_radio.generation==8);
    assert(esp32_mquickjs_wifi_radio_finish_action_recovery(&recovery)==ESP_ERR_INVALID_STATE);
    stale=recovery;
    assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&recovery,true)==0 && !recovery.identity);
    assert(esp32_mquickjs_wifi_radio_shutdown_action_recovery(&stale)==ESP_ERR_INVALID_STATE);
    assert(!esp32_mquickjs_wifi_radio_action_recovery_active(&stale));
    assert(!s_radio.lifecycle.identity && !locks && !critical);
    free(request);return 0;
}
'''
