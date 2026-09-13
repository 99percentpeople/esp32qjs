"""Deferred real RSSI write history/status conversion; no armed/RF simulation.

The production owner gate, setter and status getter are used with SDK/native
storage boundaries. Physical-generation changes below are isolated state inputs,
not evidence of actual STOP/deinit/init or event delivery.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest

from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import build, extract, run


class WiFiRssiRequest(unittest.TestCase):
    def test_actual_request_revision_error_history_and_generation(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, control_code(profile, ap) + NATIVE_MAIN)

    def test_real_configuration_checkpoint_replay_never_rearms_rssi(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                code = 'static unsigned rssi_calls;\n' + config_code(profile, True, mutation_boundary=True)
                code = code.replace('threshold=value;return sdk_step(true);',
                                    '++rssi_calls;threshold=value;return sdk_step(true);')
                code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
                compile_run(self, code + RESTART_MAIN)


class WiFiRssiRequestStatus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        code = control_code('esp32c5/representative')
        code = re.sub(r'\bcalls\b', 'native_calls', code)
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += 'static const char *esp_err_to_name(int error) { return error==ESP_OK ? "ESP_OK" : "SDK_ERROR"; }\n'
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code += extract(driver, 'esp32_mquickjs_wifi_rssi_request_to_js')
        cls.binary = build(cls.temp.name, code, VM_MAIN)

    def test_real_converter_nullable_history_failure_and_every_vm_allocation(self):
        run([str(self.binary)])


NATIVE_MAIN = fixture_text('wifi/station/test_wifi_rssi_request/native_main.inc')


RESTART_MAIN = fixture_text('wifi/station/test_wifi_rssi_request/restart_main.inc')


VM_MAIN = fixture_text('wifi/station/test_wifi_rssi_request/vm_main.inc')
