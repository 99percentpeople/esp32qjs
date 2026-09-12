"""Deferred real pre-STOP observations; SDK/lock/storage boundaries injected."""
import re
import unittest

from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def snapshot_code(profile):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = config_code(profile, True)
    code += extract(radio, 'esp32_mquickjs_wifi_radio_stop_snapshot')
    code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
    return code


class WiFiStopSnapshot(unittest.TestCase):
    def test_real_observation_partial_outputs_failure_history_and_exact_units(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, snapshot_code(profile) + MAIN)

    def test_real_sdk_mutation_boundary_and_stopped_freshness(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        boundary = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio_mutation.h').read_text()
        helpers = MAIN[:MAIN.index('int main(void)')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                # Install real call-site aliases after the injected SDK function
                # definitions, matching production's post-declaration include.
                code = snapshot_code(profile)
                code += extract(radio, 'wifi_radio_invalidate_stop_snapshot_locked')
                code += boundary + helpers + MUTATION_MAIN
                compile_run(self, code)

    def test_radio_sdk_writer_inventory_has_invalidation_boundary(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        boundary = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio_mutation.h').read_text()
        calls = set(re.findall(r'\besp_wifi_[A-Za-z0-9_]+(?=\()', radio))
        writers = {name for name in calls if name.startswith(('esp_wifi_set_', 'esp_wifi_config_', 'esp_wifi_disable_', 'esp_wifi_enable_'))
                   or name in ('esp_wifi_init', 'esp_wifi_start', 'esp_wifi_deinit', 'esp_wifi_restore',
                               'esp_wifi_coex_pwr_configure', 'esp_wifi_connectionless_module_set_wake_interval',
                               'esp_wifi_action_tx_req', 'esp_wifi_remain_on_channel',
                               'esp_wifi_ftm_initiate_session', 'esp_wifi_ftm_end_session', 'esp_wifi_ftm_resp_set_offset')}
        aliases = set(re.findall(r'^#define (esp_wifi_\w+)\(', boundary, re.M))
        self.assertEqual(aliases, writers)
        neutral = set(re.findall(r'^#define (esp_wifi_\w+)\([^\n]*WIFI_RADIO_STOP_NEUTRAL', boundary, re.M))
        self.assertEqual(neutral, {'esp_wifi_set_storage', 'esp_wifi_set_rssi_threshold', 'esp_wifi_set_event_mask',
                                  'esp_wifi_ftm_resp_set_offset', 'esp_wifi_enable_rx_statistics', 'esp_wifi_enable_tx_statistics'})
        self.assertNotIn('esp_wifi_stop', aliases)
        self.assertFalse(any(name.startswith('esp_wifi_get_') for name in aliases))


MAIN = r'''
static esp32_mquickjs_wifi_radio_stop_snapshot_t read_snapshot(void) {
    esp32_mquickjs_wifi_radio_stop_snapshot_t result;
    esp32_mquickjs_wifi_radio_stop_snapshot(&result);return result;
}
static void observe(void) {
    wifi_radio_operation_lock();wifi_radio_capture_stop_snapshot_locked();wifi_radio_operation_unlock();
}
static void reset_observation(void) {
    setup();memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    s_radio.event_identity=51;calls=writes=fail_at=0;
}
int main(void) {
    reset_observation();
    esp32_mquickjs_wifi_radio_stop_snapshot_t snapshot=read_snapshot();
    assert(!snapshot.stop_identity && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_NONE);
    observe();snapshot=read_snapshot();unsigned sdk_calls=calls;
    assert(sdk_calls==9 && !writes && !native_stops && !native_starts && !allocations);
    assert(snapshot.inactive_time[0]==17 && !snapshot.inactive_time[1]);
    assert(snapshot.generation==7 && snapshot.stop_identity==51 && snapshot.error==ESP_OK);
    assert(snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE && snapshot.tx_power==52);
    assert(snapshot.mode==WIFI_MODE_STA && snapshot.band==WIFI_BAND_2G && snapshot.primary==1 && snapshot.secondary==WIFI_SECOND_CHAN_NONE);
    assert(s_radio.started && !s_radio.fault_stage && !s_radio.cleanup_stage);
    s_radio.started=false;s_radio.generation=8;native_tx_power=80;native_home=11;
    esp32_mquickjs_wifi_radio_stop_snapshot_t historical=read_snapshot();
    assert(historical.generation==snapshot.generation && historical.stop_identity==snapshot.stop_identity &&
        historical.tx_power==snapshot.tx_power && historical.primary==snapshot.primary &&
        historical.error==ESP_OK && calls==sdk_calls); /* Historical, not a current getter. */
    esp32_mquickjs_wifi_radio_stop_snapshot(NULL);assert(calls==sdk_calls);

    for(unsigned failure=1;failure<=sdk_calls;++failure) {
        reset_observation();fail_at=failure;observe();snapshot=read_snapshot();
        if(failure==1) {
            /* Scan history has its own error; native STOP must still proceed
             * with valid power/channel/inactive observations. */
            assert(snapshot.error==ESP_OK && !s_scan_parameters.known && s_scan_parameters.error==77);
            assert(calls==sdk_calls && !writes && !s_radio.fault_stage);
            continue;
        }
        assert(snapshot.error==77 && snapshot.stop_identity==51 && snapshot.generation==7);
        assert(snapshot.step!=ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE);
        assert(!snapshot.tx_power && !snapshot.primary && !snapshot.band_mode && !snapshot.band && !snapshot.secondary);
        assert(!snapshot.inactive_time[0] && !snapshot.inactive_time[1]);
        assert(calls==failure && !writes && !s_radio.fault_stage && !s_radio.cleanup_stage && s_radio.started);
    }
    reset_observation();native_tx_power=0;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_POWER && !snapshot.tx_power);
    reset_observation();inactive[0]=2;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_STA_INACTIVE);
    assert(!snapshot.inactive_time[0] && !snapshot.tx_power);
    reset_observation();s_radio.effective_mode=native_mode=WIFI_MODE_APSTA;
    observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_OK && snapshot.inactive_time[0]==17 && snapshot.inactive_time[1]==601 && calls==10);
    reset_observation();native_home=11;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_VERIFY && !snapshot.primary);
    reset_observation();native_secondary=native_home_secondary=(wifi_second_chan_t)99;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && !snapshot.secondary);
    reset_observation();s_radio.started=false;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_STATE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_ADMISSION && !calls);
    reset_observation();s_radio.fault_stage="uncertain";observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_STATE && !calls && !strcmp(s_radio.fault_stage,"uncertain"));
#if CONFIG_SOC_WIFI_SUPPORT_5G
    reset_observation();native_band_mode=WIFI_BAND_MODE_5G_ONLY;native_band=WIFI_BAND_5G;native_channel=native_home=36;
    observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_OK && snapshot.band==WIFI_BAND_5G && snapshot.band_mode==WIFI_BAND_MODE_5G_ONLY && snapshot.primary==36);
#endif
    assert(!locks && !critical && !helper_locks && !allocation);
    return 0;
}
'''


MUTATION_MAIN = r'''
/* The SDK can fail after a setter is attempted; its result must never preserve
 * the old STOP observation merely because this setter missed the inventory. */
static int (esp_wifi_enable_bsscolor_collision_detection)(wifi_interface_t interface,bool enabled) {
    assert(interface==WIFI_IF_STA && enabled);return sdk_step(true);
}
static unsigned argument_calls;
static wifi_mode_t next_mode(void) {++argument_calls;return WIFI_MODE_STA;}
static void stopped_candidate(void) {
    reset_observation();observe();
    assert(!read_snapshot().unchanged); /* Native STOP and fence have not completed. */
    s_radio.started=false;s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(read_snapshot().unchanged);
}
int main(void) {
    stopped_candidate();
    unsigned before=calls;wifi_ps_type_t ps;
    wifi_radio_operation_lock();assert(esp_wifi_get_ps(&ps)==ESP_OK);wifi_radio_operation_unlock();
    assert(read_snapshot().unchanged && calls==before+1);
    s_radio.stop_required=true;assert(!read_snapshot().unchanged);s_radio.stop_required=false;
    s_radio.generation++;assert(!read_snapshot().unchanged);s_radio.generation--;
    s_radio.event_identity++;assert(!read_snapshot().unchanged);s_radio.event_identity--;
    s_radio.storage_configured=false;assert(!read_snapshot().unchanged);s_radio.storage_configured=true;
    s_radio.cleanup_stage="stop-events";assert(!read_snapshot().unchanged);s_radio.cleanup_stage=NULL;
    assert(read_snapshot().unchanged);
    fail_at=calls+1;argument_calls=0;
    wifi_radio_operation_lock();assert(esp_wifi_set_mode(next_mode())==77);wifi_radio_operation_unlock();
    assert(argument_calls==1 && !read_snapshot().unchanged);
    esp32_mquickjs_wifi_radio_stop_snapshot_t historical=read_snapshot();
    assert(historical.error==ESP_OK && historical.tx_power==52 && historical.stop_identity==51);
    fail_at=0;
    wifi_radio_operation_lock();assert(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK);wifi_radio_operation_unlock();
    assert(!read_snapshot().unchanged); /* A successful rollback does not renew proof. */
    stopped_candidate();fail_at=calls+1;
    wifi_radio_operation_lock();assert(esp_wifi_enable_bsscolor_collision_detection(WIFI_IF_STA,true)==77);wifi_radio_operation_unlock();
    assert(!read_snapshot().unchanged);fail_at=0;
    stopped_candidate();
    wifi_radio_operation_lock();assert(esp_wifi_set_storage(WIFI_STORAGE_RAM)==ESP_OK);wifi_radio_operation_unlock();
    assert(read_snapshot().unchanged); /* Reviewed policy-only writer preserves RF history. */
    wifi_radio_operation_lock();assert(esp_wifi_set_rssi_threshold(-75)==ESP_OK);wifi_radio_operation_unlock();
    assert(read_snapshot().unchanged); /* Observer request is not a saved RF setting. */
    stopped_candidate();
    wifi_radio_operation_lock();wifi_radio_invalidate_stop_snapshot_locked();wifi_radio_operation_unlock();
    assert(!read_snapshot().unchanged);
    assert(!locks && !critical && !helper_locks && !allocation);
    return 0;
}
'''
