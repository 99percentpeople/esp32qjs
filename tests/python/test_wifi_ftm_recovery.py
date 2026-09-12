"""Deferred production FTM physical recovery and original Session retirement.

Real Radio admission/STOP/shutdown/timer barriers/retirement plus real Session
worker. SDK, event/timer/worker scheduling, locks and unrelated broker storage are
injected boundaries. No replacement recovery FSM or SDK/RF proof. AST only.
"""
import unittest
from test_wifi_ftm_radio import ftm_code, MAIN as FTM_MAIN
from test_wifi_action_recovery import BOUNDARIES as RECOVERY_BOUNDARIES
from test_wifi_ftm_session import BOUNDARIES as SESSION_BOUNDARIES, MAIN as SESSION_MAIN
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract
from test_wifi_recovery_runtime import recovery_request_code


def recovery_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = '#define CONFIG_IDF_TARGET_' + profile.split('/')[0].upper() + ' 1\n' + ftm_code(profile, ap)
    code = code.replace('bool uncertain;} s_interval;', 'bool uncertain,restore_pending;} s_interval;')
    code = code.replace('struct {unsigned identity,generation;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('bool event_fence_posted,event_fence_seen;', '''bool event_fence_posted,event_fence_seen;
        bool stop_submitted,ap_reopen_pending,ap_reopen_attempted,ap_reopen_quiesced,ap_reopen_restore_complete;
        wifi_mode_t event_live;unsigned ap_stop_phase;
        unsigned ap_transition_application,ap_transition_station,ap_transition_access_point;
        esp_err_t ap_stop_error,cleanup_error,channel_observation_error;
        uint64_t channel_observation_revision;uint8_t primary_channel;wifi_second_chan_t secondary_channel;''')
    code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
    code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
    boundaries = RECOVERY_BOUNDARIES.replace('s_action.lease.acquired', 's_ftm.lease.acquired')
    code += boundaries
    code += 'static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *);\n'
    code += extract(source, 'wifi_radio_ftm_recovery_timer_locked')
    for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault', 'wifi_radio_action_recovery_exact_locked',
                 'wifi_radio_ftm_recovery_capture_owner_locked', 'esp32_mquickjs_wifi_radio_ftm_recovery_active',
                 'esp32_mquickjs_wifi_radio_begin_ftm_recovery', 'wifi_radio_stop_owners_locked', 'wifi_radio_stop_lease_locked', 'wifi_radio_stop_locked',
                 'wifi_radio_shutdown_lease_locked', 'wifi_radio_shutdown_locked', 'wifi_radio_ftm_recovery_phase',
                 'esp32_mquickjs_wifi_radio_stop_ftm_recovery', 'esp32_mquickjs_wifi_radio_shutdown_ftm_recovery',
                 'esp32_mquickjs_wifi_radio_check_stopped_ftm_recovery', 'esp32_mquickjs_wifi_radio_finish_ftm_recovery'):
        code += extract(source, name)
    # Route through production kind selection and both native exact-token guards.
    # Checkpoint snapshot/replay storage remains covered by its separate fixtures.
    code = code.replace('typedef struct {uint32_t generation;} esp32_mquickjs_wifi_raw_tx_broker_status_t;',
        'typedef struct {uint32_t generation;struct {uint32_t generation,identity,radio_lease_identity;} token;} esp32_mquickjs_wifi_raw_tx_broker_status_t;')
    # No Raw TX owner in this fixture: unrelated subsystem admission rejects.
    code += r"""
static bool esp32_mquickjs_wifi_radio_raw_tx_recovery_active(const esp32_mquickjs_wifi_radio_lifecycle_t *token) {(void)token;return false;}
static int esp32_mquickjs_wifi_radio_begin_raw_tx_recovery(const void *a,const void *s,const void *p,const void *o,void *t,void *m) {
    (void)a;(void)s;(void)p;(void)o;(void)t;(void)m;return ESP_ERR_INVALID_STATE;
}
"""
    for phase in ('stop','check_stopped','shutdown','finish'):
        code += 'static int esp32_mquickjs_wifi_radio_' + phase + '_raw_tx_recovery(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return ESP_ERR_INVALID_STATE;}\n'
    code += recovery_request_code()
    for name in ('esp32_mquickjs_wifi_radio_action_recovery_active',
                 'esp32_mquickjs_wifi_radio_check_stopped_action_recovery',
                 'esp32_mquickjs_wifi_radio_begin_action_recovery',
                 'wifi_radio_action_recovery_phase', 'esp32_mquickjs_wifi_radio_stop_action_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_action_recovery',
                 'esp32_mquickjs_wifi_radio_finish_action_recovery',
                 'esp32_mquickjs_wifi_radio_begin_recovery', 'esp32_mquickjs_wifi_radio_recovery_active',
                 'esp32_mquickjs_wifi_radio_stop_recovery', 'esp32_mquickjs_wifi_radio_check_stopped_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_recovery', 'esp32_mquickjs_wifi_radio_finish_recovery'):
        code += extract(source, name)
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_ftm_session.h')
    code += SESSION_BOUNDARIES
    code += unit(COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_session.c')
    code += FTM_MAIN[:FTM_MAIN.index('int main(void)')]
    # Real Session worker/scheduler helpers, without the unrelated close-hook helper.
    helpers = SESSION_MAIN[SESSION_MAIN.index('static void work(void)'):SESSION_MAIN.index('int main(void)')]
    return code + helpers


class WiFiFtmRecovery(unittest.TestCase):
    def test_missing_or_ambiguous_report_physical_retirement_and_cleanup_suffix(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, recovery_code(profile, ap) + MAIN)


MAIN = r'''
static esp32_mquickjs_wifi_radio_lifecycle_t recovery;
static esp32_mquickjs_wifi_ftm_session_t *session;
static wifi_mode_t recovery_mode;
static int begin(const esp32_mquickjs_wifi_radio_lease_t *application) {
    esp32_mquickjs_wifi_recovery_request_t request={session->token.generation,session->token.identity,ESP32_MQUICKJS_WIFI_RECOVERY_FTM};
    return esp32_mquickjs_wifi_radio_begin_recovery(application,NULL,NULL,&request,&recovery,&recovery_mode);
}
static void setup_recovery(void) {
    reset_ftm();memset(&recovery,0,sizeof(recovery));s_radio.stop_required=true;
    s_channel_event_instance=(void *)1;atomic_store(&s_channel_callbacks,0);
    stop_error=wait_error=deinit_error=unregister_error=0;stop_calls=stop_waits=deinit_calls=unregister_calls=0;wait_expired=false;
    memset(&s_config_restart,0,sizeof(s_config_restart));memset(&s_policy_restart,0,sizeof(s_policy_restart));
    wifi_ftm_initiator_cfg_t config={.resp_mac={2,3,4,5,6,7},.channel=6,.frm_count=16};
    assert(!esp32_mquickjs_wifi_ftm_create(&config,4,&session));
    assert(!esp32_mquickjs_wifi_ftm_start(session));service();assert(owners()==1 && ftm_start_calls==1);
}
int main(void) {
    for(unsigned ambiguous=0;ambiguous<2;++ambiguous) {
        setup_recovery();esp32_mquickjs_wifi_ftm_state_t native;
        esp32_mquickjs_wifi_recovery_request_t invalid={session->token.generation,session->token.identity,99};
        assert(esp32_mquickjs_wifi_radio_begin_recovery(NULL,NULL,NULL,&invalid,&recovery,&recovery_mode)==ESP_ERR_INVALID_ARG);
        invalid.kind=ESP32_MQUICKJS_WIFI_RECOVERY_ACTION;
        assert(esp32_mquickjs_wifi_radio_begin_recovery(NULL,NULL,NULL,&invalid,&recovery,&recovery_mode)==ESP_ERR_INVALID_STATE);
        assert(!recovery.identity && !stop_calls && !deinit_calls && owners()==1);
        esp32_mquickjs_wifi_ftm_token_t wrong=session->token;++wrong.identity;
        assert(esp32_mquickjs_wifi_radio_begin_ftm_recovery(NULL,NULL,NULL,&wrong,&recovery,&recovery_mode)==ESP_ERR_INVALID_STATE);
        wrong=session->token;++wrong.generation;
        assert(esp32_mquickjs_wifi_radio_begin_ftm_recovery(NULL,NULL,NULL,&wrong,&recovery,&recovery_mode)==ESP_ERR_INVALID_STATE);
        s_radio.leases[3].identity=500;assert(begin(NULL)==ESP_ERR_INVALID_STATE && !recovery.identity);s_radio.leases[3].identity=0;
        s_radio.wake_locks=1;assert(begin(NULL)==ESP_ERR_INVALID_STATE);s_radio.wake_locks=0;
        s_interval.restore_pending=true;assert(begin(NULL)==ESP_ERR_INVALID_STATE);s_interval.restore_pending=false;
        s_tx_rate_lease.restore_pending=true;assert(begin(NULL)==ESP_ERR_INVALID_STATE);s_tx_rate_lease.restore_pending=false;
        unsigned next_lifecycle=s_radio.next_lifecycle_identity;s_radio.next_lifecycle_identity=0;
        assert(begin(NULL)==ESP_ERR_NO_MEM && !recovery.identity);s_radio.next_lifecycle_identity=next_lifecycle;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;assert(begin(NULL)==ESP_ERR_INVALID_STATE);s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        if(ambiguous) {
            terminal(); /* An ordinary timer marker exists before recovery. */
            assert(esp32_mquickjs_wifi_radio_ftm_collect(&session->token,NULL,0,0,&native)==ESP_ERR_TIMEOUT && timer_pending);
            terminal();assert(s_ftm.state.ambiguous);
        }
        assert(!begin(NULL) && recovery.identity && recovery_mode==WIFI_MODE_STA);
        assert(esp32_mquickjs_wifi_radio_recovery_active(&recovery));
        esp32_mquickjs_wifi_radio_lifecycle_t stale=recovery;++stale.identity;
        assert(esp32_mquickjs_wifi_radio_stop_recovery(&stale)==ESP_ERR_INVALID_STATE && !stop_calls);
        assert(esp32_mquickjs_wifi_radio_ftm_end(&session->token)==ESP_ERR_TIMEOUT && !ftm_end_calls);
        assert(esp32_mquickjs_wifi_radio_ftm_collect(&session->token,NULL,0,0,&native)==ESP_ERR_TIMEOUT && !report_calls);
        assert(esp32_mquickjs_wifi_radio_ftm_retire(&session->token,&native)==ESP_ERR_TIMEOUT && owners()==1);
        assert(esp32_mquickjs_wifi_radio_check_stopped_recovery(&recovery)==ESP_ERR_INVALID_STATE);
        stop_error=77;assert(esp32_mquickjs_wifi_radio_stop_recovery(&recovery)==77 && !s_ftm.recovery_stopped);
        stop_error=0;wait_error=ESP_ERR_TIMEOUT;
        assert(esp32_mquickjs_wifi_radio_stop_recovery(&recovery)==ESP_ERR_TIMEOUT && stop_calls==2 && !s_ftm.recovery_stopped);
        wait_error=0;assert(!esp32_mquickjs_wifi_radio_stop_recovery(&recovery) && stop_calls==2 && s_ftm.recovery_stopped);
        assert(!esp32_mquickjs_wifi_radio_check_stopped_recovery(&recovery));
        assert(!s_ftm.state.physical_termination && !deinit_calls);
        unsigned before=timer_starts;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery)==ESP_ERR_TIMEOUT && !deinit_calls);
        if(ambiguous) {assert(timer_starts==before);timer_fire();assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery)==ESP_ERR_TIMEOUT);}
        assert(timer_starts==before+1 && timer_pending);timer_fire();
        fence_error=66;assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery)==66 && !deinit_calls);
        assert(s_ftm.recovery_timer_ready && !s_ftm.recovery_sdk_ready);
        before=timer_starts;fence_error=0;unregister_error=88;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery)==88 && timer_starts==before && !deinit_calls);
        assert(s_ftm.recovery_sdk_ready);unsigned fenced=fence_calls;
        unregister_error=0;deinit_error=99;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery)==99 && deinit_calls==1 && !s_ftm.state.physical_termination);
        assert(fence_calls==fenced && timer_starts==before);
        deinit_error=0;
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery)==ESP_ERR_TIMEOUT && deinit_calls==2);
        assert(s_ftm.state.physical_termination && s_ftm.state.report_discarded && s_ftm.state.report_consumed);
        assert(s_radio.generation==recovery.generation && owners()==1 && !s_radio.driver_owned);
        assert(esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery)==ESP_ERR_TIMEOUT && deinit_calls==2);
        assert(esp32_mquickjs_wifi_radio_finish_recovery(&recovery)==ESP_ERR_INVALID_STATE);
        /* Only the original native Session worker consumes the exact lease. */
        service();esp32_mquickjs_wifi_ftm_status_t status;esp32_mquickjs_wifi_ftm_status(session,&status);
        assert(status.retired && status.native.physical_termination && !status.report_ready && status.error);
        assert(status.native.ambiguous==(ambiguous!=0) && status.native.terminal==(ambiguous!=0));
        assert(!status.retained_entries && !session->entries && !owners() && !report_calls && !ftm_end_calls);
        wifi_ftm_report_entry_t entry;assert(!esp32_mquickjs_wifi_ftm_report_entry(session,0,&entry));
        assert(!esp32_mquickjs_wifi_radio_shutdown_recovery(&recovery) && deinit_calls==2 && s_radio.generation==recovery.generation+1);
        assert(!esp32_mquickjs_wifi_radio_finish_recovery(&recovery) && s_radio.lifecycle.identity==recovery.identity);
        assert(!esp32_mquickjs_wifi_radio_recovery_active(&recovery));
        esp32_mquickjs_wifi_ftm_release(session);session=NULL;empty();
    }
    return 0;
}
'''
