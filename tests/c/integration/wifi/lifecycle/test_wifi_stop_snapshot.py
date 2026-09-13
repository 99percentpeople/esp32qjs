"""Deferred real pre-STOP observations; SDK/lock/storage boundaries injected."""
from tests.support.fixtures import fixture_text
import re
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


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


MAIN = fixture_text('wifi/lifecycle/test_wifi_stop_snapshot/main.inc')


MUTATION_MAIN = fixture_text('wifi/lifecycle/test_wifi_stop_snapshot/mutation_main.inc')
