"""Deferred real stopped-mode transaction and public binding; SDK/VM boundaries injected."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_storage import storage_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def mode_code(profile, ap=True):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    return storage_code(profile, ap) + BOUNDARIES + extract(source, 'esp32_mquickjs_wifi_radio_set_mode') + RESET


class WiFiDriverMode(unittest.TestCase):
    def test_production_admission_readback_rollback_and_persistence_diagnostics(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, mode_code(profile, ap) + MAIN)

    def test_public_mode_strings_arity_original_errors_and_gc(self):
        code = re.sub(r'\bcalls\b', 'native_calls', mode_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('driver_phy_write_error', 'js_wifi_driver_set_mode'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('"off"', True), ('"station"', True), ('"softAP"', True),
                                      ('"station+softAP"', True), ('"ap"', False), ('"apsta"', False),
                                      ('"none"', False), ('"nan"', False), ('"Station"', False),
                                      ('"station\\u0000suffix"', False), ('0', False), ('true', False), ('{}', False)]:
                run([str(binary), expression, str(int(valid)), 'normal'])
            for scenario in ('arity-zero', 'arity-two', 'sdk-error'):
                run([str(binary), '"station"', '0', scenario])


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_mode/boundaries.inc')

RESET = fixture_text('wifi/driver/test_wifi_driver_mode/reset.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_mode/main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_mode/vm_main.inc')
