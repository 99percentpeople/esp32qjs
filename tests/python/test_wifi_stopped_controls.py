"""Deferred real storage/RSSI/event-mask writes, STOP history, admission and config replay.

SDK, physical generation and lock boundaries are injected. The mutation aliases,
Radio setters, STOP predicate, owner registry admission and configuration replay
are production code. No RF, NVS or physical-lifecycle proof is inferred.
"""
import unittest

from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract
from test_wifi_config_controls import structure


def stopped_controls_code(profile, ap=True, reviewed=True):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = config_code(profile, ap, mutation_boundary=True)
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += structure(header, 'esp32_mquickjs_wifi_radio_restart_selection_t')
    code += '#define ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B 2\nstatic struct {struct {bool configured;} records[4];} s_policies;\n'
    code = code.replace('struct {unsigned identity,generation;} operation,lifecycle;',
                        'struct {unsigned identity,generation;} operation;'
                        'esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;uint32_t next_lifecycle_identity;')
    if not reviewed:
        code = code.replace(f'#define CONFIG_IDF_TARGET_{profile.split("/")[0].upper()} 1', '')
    # Move this production definition after the real SDK aliases. The shared
    # control fixture normally isolates it from the Radio-local macro boundary.
    control = extract(code, 'esp32_mquickjs_wifi_radio_connection_control')
    code = code.replace(control, control[:control.index('\n{')] + ';\n')
    code += control
    code += ''.join(extract(radio, name) for name in (
        'wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked', 'esp32_mquickjs_wifi_radio_begin_stopped_restart',
        'esp32_mquickjs_wifi_radio_set_storage', 'esp32_mquickjs_wifi_radio_event_mask'))
    code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
    return code + HELPERS


class WiFiStoppedControls(unittest.TestCase):
    def test_reviewed_writes_keep_history_and_capture_new_storage(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, stopped_controls_code(profile, ap) + MAIN)

    def test_mask_capture_rollback_and_fault_admission(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, stopped_controls_code(profile, ap) + MASK_MAIN)

    def test_unreviewed_target_keeps_writer_invalidation(self):
        compile_run(self, stopped_controls_code('esp32c5/representative', reviewed=False) + FALLBACK_MAIN)


HELPERS = r'''
static bool unchanged(void) {
    wifi_radio_operation_lock();bool valid=wifi_radio_stop_snapshot_unchanged_locked();
    wifi_radio_operation_unlock();return valid;
}
static void stopped(void) {
    setup();memset(&s_policy_restart,0,sizeof(s_policy_restart));
    s_radio.event_identity=51;
    wifi_radio_operation_lock();wifi_radio_capture_stop_snapshot_locked();
    assert(wifi_radio_stop_locked()==ESP_OK);wifi_radio_operation_unlock();
    /* End the isolated STOP reservation, as the public stop coordinator does. */
    memset(&s_radio.lifecycle,0,sizeof(s_radio.lifecycle));memset(&token,0,sizeof(token));
    memset(&s_wifi_application,0,sizeof(s_wifi_application));
    memset(&s_wifi_state.radio_lease,0,sizeof(s_wifi_state.radio_lease));memset(&s_ap_lease,0,sizeof(s_ap_lease));
    s_radio.next_lifecycle_identity=41;calls=writes=fail_at=0;assert(unchanged());
}
static int rssi(void) {
    int32_t actual;
    return esp32_mquickjs_wifi_apply_connection_control(
        ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result);
}
static void reject(void) {
    esp32_mquickjs_wifi_radio_restart_selection_t mode={.mode=WIFI_MODE_AP};
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&mode)==ESP_ERR_INVALID_STATE);
    assert(!token.identity && !token.generation && mode.mode==WIFI_MODE_NULL && !s_radio.lifecycle.identity);
}
static void admit_without_history(void) {
    esp32_mquickjs_wifi_radio_restart_selection_t mode={0};
    assert(!unchanged());
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&mode)==ESP_OK);
    assert(mode.mode==WIFI_MODE_STA && token.identity==41 && !s_radio.started);
}
static void reconstruct(wifi_storage_t storage) {
    uint32_t expected_mask=native_event_mask;
    esp32_mquickjs_wifi_radio_restart_selection_t mode={0};
    assert(esp32_mquickjs_wifi_radio_begin_stopped_restart(&token,&mode)==ESP_OK && mode.mode==WIFI_MODE_STA);
    wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,mode.mode)==ESP_OK);
    assert(s_config_restart.snapshot->storage==storage && s_config_restart.snapshot->tx_power==52);
    assert(s_config_restart.snapshot->event_mask==expected_mask);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,mode.mode)==ESP_OK);
    /* Physical START/event delivery remains the fixture boundary. */
    s_radio.started=s_radio.stop_required=native_running=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_commit_locked(&token,mode.mode)==ESP_OK);
    assert(s_radio.storage==storage && native_storage==storage && native_tx_power==52 && native_channel==1);
    assert(native_event_mask==expected_mask);
    wipe();wifi_radio_operation_unlock();
}
'''


MAIN = r'''
int main(void) {
    const wifi_storage_t values[]={WIFI_STORAGE_RAM,WIFI_STORAGE_FLASH};
    for(unsigned i=0;i<2;++i) {
        stopped();esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(esp32_mquickjs_wifi_radio_set_storage(values[i],&result)==ESP_OK);
        assert(unchanged() && calls==1 && !memcmp(&before,&s_stop_snapshot,sizeof(before)));
        reconstruct(values[i]);
    }
    stopped();fail_at=1;
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==77);
    assert(!unchanged() && s_stop_snapshot.unchanged && !s_radio.storage_configured);
    unsigned attempts=calls;reject();assert(calls==attempts);
    fail_at=0;assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(unchanged() && !s_radio.fault_stage && s_radio.storage_configured);
    reconstruct(WIFI_STORAGE_RAM);
    for(unsigned failure=0;failure<2;++failure) {
        stopped();fail_at=failure ? 1 : 0;
        esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(rssi()==(failure ? 77 : ESP_OK));
        assert(unchanged() && calls==1 && !memcmp(&before,&s_stop_snapshot,sizeof(before)));
        esp32_mquickjs_wifi_rssi_request_t request=s_rssi_request;
        fail_at=0;reconstruct(WIFI_STORAGE_FLASH);
        assert(!memcmp(&request,&s_rssi_request,sizeof(request))); /* No RSSI replay. */
    }
    stopped();memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(rssi()==ESP_OK && !unchanged());admit_without_history(); /* Capture must obtain new runtime observations. */
    stopped();wifi_radio_operation_lock();assert(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(rssi()==ESP_OK && !unchanged());admit_without_history(); /* Does not revive the old history. */
    stopped();++s_radio.event_identity;
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK && !unchanged());admit_without_history();
    stopped();assert(rssi()==ESP_OK);s_radio.leases[0].identity=9;
    reject(); /* Preserved source evidence never bypasses a new owner. */
    assert(!allocation && !locks && !critical && !helper_locks);return 0;
}
'''


FALLBACK_MAIN = r'''
int main(void) {
    stopped();assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(!unchanged());admit_without_history();
    stopped();assert(rssi()==ESP_OK && !unchanged());admit_without_history();
    stopped();uint32_t actual=0;
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==ESP_OK && !unchanged());admit_without_history();
    assert(!allocation && !locks && !critical && !helper_locks);return 0;
}
'''


MASK_MAIN = r'''
int main(void) {
    uint32_t actual=99;
    for(unsigned requested=0;requested<2;++requested) {
        stopped();native_event_mask=1-requested;
        esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(esp32_mquickjs_wifi_radio_event_mask(true,requested,&actual,&result)==ESP_OK);
        assert(actual==requested && calls==3 && writes==1 && unchanged());
        assert(!memcmp(&before,&s_stop_snapshot,sizeof(before)));
        reconstruct(WIFI_STORAGE_FLASH); /* Capture/replay the new mask, not the STOP-time value. */
    }
    for(unsigned failure=1;failure<=3;++failure) {
        stopped();fail_at=failure;
        esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==77);
        assert(unchanged() && !memcmp(&before,&s_stop_snapshot,sizeof(before)) && native_event_mask==0);
        assert(result.rollback_attempted==(failure!=1));
        assert(result.rollback_complete==(failure!=1));
        fail_at=0;reconstruct(WIFI_STORAGE_FLASH);
    }
    for(unsigned failure=1;failure<=2;++failure) {
        stopped();fail_at=2;rollback_fail=failure;
        assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==77);
        assert(result.rollback_attempted && !result.rollback_complete && result.rollback_error==88);
        assert(s_stop_snapshot.unchanged && !unchanged());
        unsigned attempts=calls;reject();assert(calls==attempts); /* Preserved RF history cannot clear a fault. */
    }
    stopped();native_event_mask=2;fail_at=2;
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==77);
    assert(!result.rollback_attempted && writes==1 && !unchanged());reject();
    stopped();assert(esp32_mquickjs_wifi_radio_event_mask(true,2,&actual,&result)==ESP_ERR_INVALID_ARG);
    assert(!calls && unchanged()); /* Required completion-event bits remain protected. */
    stopped();assert(esp32_mquickjs_wifi_radio_event_mask(true,0,&actual,&result)==ESP_OK);
    assert(calls==1 && !writes && unchanged());
    stopped();memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==ESP_OK && !unchanged());admit_without_history();
    stopped();wifi_radio_operation_lock();assert(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==ESP_OK && !unchanged());admit_without_history();
    assert(!allocation && !locks && !critical && !helper_locks);return 0;
}
'''
