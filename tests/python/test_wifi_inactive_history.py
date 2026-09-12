"""Deferred production AP retirement observation and hidden-value checkpoint.

Uses actual lease validation/AP-stop/capture/replay helpers; SDK/event fences,
physical rebuild/start and native storage are injected. No RF/NVS proof.
"""
import re
import unittest
from test_wifi_driver_phy import COMPONENT
from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def history_code(profile):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = config_code(profile, True, mutation_boundary=True)
    code = code.replace('wifi_mode_t effective_mode;',
        'wifi_mode_t effective_mode;unsigned ap_stop_phase;esp_err_t ap_stop_error;'
        'uint32_t ap_transition_application,ap_transition_station,ap_transition_access_point;')
    old = extract(code, 'wifi_radio_begin_events')
    code = code.replace(old, 'static bool ap_close_events;\n' + old.replace(
        '    assert((phase==RADIO_EVENTS_START',
        '    if(phase==RADIO_EVENTS_AP_STOP){assert(mode==WIFI_MODE_AP && s_radio.started);ap_close_events=true;return sdk_step(false);}\n'
        '    assert((phase==RADIO_EVENTS_START', 1))
    old = extract(code, 'wifi_radio_wait_events')
    code = code.replace(old, old.replace('    assert(s_radio.started',
        '    if(ap_close_events){int err=sdk_step(false);if(err==ESP_OK)ap_close_events=false;return err;}\n'
        '    assert(s_radio.started', 1))
    old = extract(code, 'esp_wifi_set_mode')
    code = code.replace(old, old.replace('assert(!s_radio.started);',
        'assert(!s_radio.started || (native_mode==WIFI_MODE_APSTA && mode==WIFI_MODE_STA));'))
    code += re.search(r'enum \{ AP_STOP_IDLE[^}]*\};', radio).group(0)
    for name in ('wifi_radio_ap_transition_valid', 'wifi_radio_ap_stop_valid',
                 'esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle'):
        code += extract(radio, name)
    return code + CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]


class WiFiInactiveHistory(unittest.TestCase):
    def test_production_ap_stop_history_capture_and_physical_generation(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, history_code(profile) + MAIN)


MAIN = r'''
static void prepare_ap_close(void) {
    setup();ap_close_events=false;s_radio.effective_mode=native_mode=WIFI_MODE_APSTA;
    s_radio.ap_stop_phase=AP_STOP_READY;s_radio.ap_stop_error=0;
    s_radio.ap_transition_application=11;s_radio.ap_transition_station=12;s_radio.ap_transition_access_point=13;
    s_radio.leases[0]=(wifi_radio_live_lease_t){11,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION};
    s_radio.leases[1]=(wifi_radio_live_lease_t){12,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA};
    s_radio.leases[2]=(wifi_radio_live_lease_t){13,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP};
}
static void after_retirement(void) {
    assert(s_radio.ap_stop_phase==AP_STOP_QUIESCED && native_mode==WIFI_MODE_STA);
    /* Runtime owner retirement is a storage boundary for this isolated unit. */
    memset(s_radio.leases,0,sizeof(s_radio.leases));s_radio.ap_stop_phase=AP_STOP_IDLE;
    calls=writes=fail_at=0;
}
int main(void) {
    prepare_ap_close();
    esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&stale)==ESP_ERR_INVALID_STATE && !calls);
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
    assert(s_inactive_history.generation==7 && s_inactive_history.known==2 && s_inactive_history.values[1]==601);
    unsigned before=calls;
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK && calls==before);
    after_retirement();wifi_radio_operation_lock();
    /* A disabled target now retains the value for deferred restoration. */
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_APSTA && !writes);
    wipe();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_mask==WIFI_MODE_STA);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_APSTA);
    assert(s_config_restart.snapshot->inactive_time[1]==601);
    new_driver();assert(!s_inactive_history.known && !s_inactive_history.generation);
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(inactive[0]==17 && inactive[1]==601 && !inactive_nvs_writes);
    assert(s_inactive_history.generation==8 && s_inactive_history.known==3);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_APSTA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();

    prepare_ap_close();fail_at=2; /* Getter failure must not prevent AP closing. */
    assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK && !s_radio.fault_stage);
    assert(s_inactive_history.failed==2 && !s_inactive_history.known && s_inactive_history.error==77);
    after_retirement();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_APSTA)==77);
    assert(!allocation && !writes && !strcmp(s_radio.configuration.stage,"restart-hidden-inactive-history"));
    wifi_radio_operation_unlock();

    prepare_ap_close();assert(esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle(&token)==ESP_OK);
    after_retirement();wifi_radio_operation_lock();
    s_inactive_history.generation--; /* Old physical history never enters a new checkpoint. */
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_STA && !s_config_restart.snapshot->inactive_time[1]);
    wipe();wifi_radio_operation_unlock();
    assert(!locks && !critical && !helper_locks && !allocation);
    return 0;
}
'''
