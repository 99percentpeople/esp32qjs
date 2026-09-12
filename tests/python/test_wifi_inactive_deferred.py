"""Deferred production hidden-value handoff and activation restoration.

SDK/physical driver storage and locks are injected. Explicit fault clearing in
one case isolates the retained readback suffix; it is not public recovery proof.
"""
import unittest
from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from test_wireless_control_regression import compile_run


class WiFiInactiveDeferred(unittest.TestCase):
    def test_frozen_handoff_activation_no_duplicate_write_and_next_restart(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                code = config_code(profile, True, mutation_boundary=True)
                code = code.replace('assert(mode==WIFI_MODE_STA &&',
                    'assert((mode==WIFI_MODE_STA || mode==WIFI_MODE_AP || mode==WIFI_MODE_APSTA) &&')
                code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
                compile_run(self, code + MAIN)


MAIN = r'''
static void prepare_hidden(wifi_storage_t storage) {
    setup();wifi_radio_operation_lock();
    native_mode=s_radio.effective_mode=WIFI_MODE_APSTA;
    s_radio.storage=native_storage=storage;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_APSTA);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(inactive[0]==17 && inactive[1]==300 && !inactive_writes[1]);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_inactive_history.generation==8 && s_inactive_history.pending==2 && !s_inactive_history.attempted);
    assert(s_inactive_history.pending_values[1]==601 && !(s_inactive_history.known&2));
    assert(s_radio.storage==storage && !inactive_nvs_writes);
}
static void enable_ap(void) {
    /* SDK activation boundary for direct restoration tests. */
    native_mode=s_radio.effective_mode=WIFI_MODE_APSTA;
    calls=writes=fail_at=0;
}
static void clear_probe_fault(void) {
    s_radio.fault_stage=s_radio.cleanup_stage=NULL;s_radio.fault_error=s_radio.cleanup_error=0;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    fail_at=0;
}
int main(void) {
    for(unsigned flash=0;flash<2;++flash) {
        wifi_storage_t storage=flash?WIFI_STORAGE_FLASH:WIFI_STORAGE_RAM;
        prepare_hidden(storage);wipe(); /* Pending value must outlive secret checkpoint. */
        unsigned before=calls;
        assert(wifi_radio_restore_inactive_locked(WIFI_MODE_STA)==ESP_OK && calls==before && s_inactive_history.pending==2);
        enable_ap();
        assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_OK);
        unsigned restore_calls=calls;
        assert(inactive[1]==601 && inactive_writes[1]==1 && !s_inactive_history.pending && !s_inactive_history.attempted);
        assert(!s_inactive_history.pending_values[1] && s_inactive_history.known==3);
        assert(s_radio.configuration.mutation_attempted && s_radio.configuration.persistent_mutation_possible==(bool)flash);
        assert(inactive_nvs_writes==flash && s_radio.storage==storage);
        before=calls;
        assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_OK && calls==before);
        wifi_radio_operation_unlock();

        for(unsigned failure=1;failure<=restore_calls;++failure) {
            prepare_hidden(storage);wipe();enable_ap();fail_at=failure;
            assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==77);
            assert(s_inactive_history.pending==2 && s_inactive_history.pending_values[1]==601 && s_radio.fault_stage);
            unsigned submitted=inactive_writes[1];before=calls;
            assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_ERR_INVALID_STATE && calls==before);
            assert(inactive_writes[1]==submitted);
            /* Isolate suffix semantics; normal admission remains faulted. */
            clear_probe_fault();
            assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_OK);
            assert(inactive[1]==601 && inactive_writes[1]==1 && !s_inactive_history.pending);
            wifi_radio_operation_unlock();
        }
        prepare_hidden(storage);wipe();enable_ap();inactive[1]=601;
        assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_OK && !inactive_writes[1]);
        assert(!s_radio.configuration.mutation_attempted && !s_radio.configuration.persistent_mutation_possible);
        assert(!s_inactive_history.pending);wifi_radio_operation_unlock();
    }
    prepare_hidden(WIFI_STORAGE_RAM);wipe();enable_ap();fail_at=3; /* mode/get/write */
    assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==77 && s_inactive_history.attempted==2);
    inactive[1]=300;clear_probe_fault();
    assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_ERR_INVALID_RESPONSE);
    assert(inactive_writes[1]==1 && s_inactive_history.pending_values[1]==601 && s_inactive_history.pending==2);
    wifi_radio_operation_unlock();

    prepare_hidden(WIFI_STORAGE_FLASH);wipe();enable_ap();fail_at=3;
    assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==77 && s_inactive_history.persistent==2);
    clear_probe_fault();s_radio.storage=native_storage=WIFI_STORAGE_RAM;
    assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_OK);
    assert(s_radio.configuration.persistent_mutation_possible && inactive_nvs_writes==1 && inactive_writes[1]==1);
    wifi_radio_operation_unlock();

    prepare_hidden(WIFI_STORAGE_RAM);wipe();
    /* A second restart while AP stays disabled must freeze the pending intent,
     * even if an unrelated observation of AP is unavailable. */
    wifi_radio_inactive_history_record(1,0,77);
    assert(s_inactive_history.failed==2 && s_inactive_history.pending_values[1]==601);
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_APSTA && s_config_restart.snapshot->inactive_time[1]==601);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_inactive_history.generation==9 && s_inactive_history.pending_values[1]==601);
    wipe();wifi_radio_operation_unlock();

    prepare_hidden(WIFI_STORAGE_RAM);
    assert(wifi_radio_stop_locked()==ESP_OK);
    assert(esp_wifi_set_mode(WIFI_MODE_AP)==ESP_OK);s_radio.effective_mode=WIFI_MODE_AP;
    /* Real START helper applies pending intent after its event barrier. */
    assert(wifi_radio_start_stopped_locked(WIFI_MODE_AP, true)==ESP_OK);
    assert(!s_inactive_history.pending && inactive[1]==601 && inactive_writes[1]==1);
    wipe();wifi_radio_operation_unlock();

    prepare_hidden(WIFI_STORAGE_RAM);wipe();enable_ap();s_inactive_history.generation--;
    assert(wifi_radio_restore_inactive_locked(WIFI_MODE_APSTA)==ESP_ERR_INVALID_STATE && !calls && !inactive_writes[1]);
    assert(s_inactive_history.pending_values[1]==601);wifi_radio_operation_unlock();
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
'''
