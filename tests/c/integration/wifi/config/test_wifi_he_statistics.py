"""Deferred production HE Radio admission, STOP intent and restore suffix tests.

Only SDK calls and scheduler/owner storage are injected. Native allocation and
Wi-Fi-task snapshot behavior are covered separately, not replaced by this fixture.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def he_support(radio):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_he_statistics.h').read_text()
    sdk = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_he_statistics_sdk.c').read_text()
    code = '\n#define ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE 1\n'
    code += structure(header, 'esp32_mquickjs_wifi_he_statistics_t')
    code += extract(header, 'wifi_he_statistics_equal')
    code += re.search(r'static struct \{[^}]*\} s_he_statistics;', radio).group(0)
    code += BOUNDARIES + extract(sdk, 'esp32_mquickjs_wifi_he_statistics_restore')
    for name in ('wifi_radio_retire_he_statistics_locked', 'wifi_radio_restore_he_statistics_locked'):
        code += extract(radio, name)
    return code


class WiFiHeStatistics(unittest.TestCase):
    def test_exact_owners_failed_write_observation_stop_and_restore(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        code = control_code('esp32c5/representative') + he_support(radio)
        for name in ('wifi_radio_he_statistics_admission_locked', 'esp32_mquickjs_wifi_radio_read_he_statistics',
                     'esp32_mquickjs_wifi_radio_write_he_statistics'):
            code += extract(radio, name)
        code += extract(wifi, 'esp32_mquickjs_wifi_apply_he_statistics')
        compile_run(self, code + MAIN)

    def test_actual_restart_capture_and_replay(self):
        from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
        code = config_code('esp32c5/representative', True, mutation_boundary=True, he_statistics=True)
        code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        compile_run(self, code + fixture_text('wifi/config/test_wifi_he_statistics/test_actual_restart_capture_and_replay.inc'))


BOUNDARIES = fixture_text('wifi/config/test_wifi_he_statistics/boundaries.inc')


MAIN = fixture_text('wifi/config/test_wifi_he_statistics/main.inc')
