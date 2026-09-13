"""Deferred production storage selection and explicit repair; SDK/lock/VM injection only."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def storage_code(profile, ap=True):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = control_code(profile, ap)
    code = code.replace('wifi_mode_t effective_mode;',
                        'wifi_mode_t effective_mode;bool stop_required;')
    code = code.replace('static struct {unsigned identity;} s_tx_rate_lease;',
                        'static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;')
    return code + BOUNDARIES + extract(source, 'esp32_mquickjs_wifi_radio_set_storage') + RESET


class WiFiDriverStorage(unittest.TestCase):
    def test_production_admission_uncertainty_and_explicit_replacement(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, storage_code(profile, ap) + MAIN)

    def test_public_strict_strings_arity_error_gc_and_no_result_allocation(self):
        code = re.sub(r'\bcalls\b', 'native_calls', storage_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('driver_phy_write_error', 'js_wifi_driver_set_storage'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('"ram"', True), ('"flash"', True), ('"RAM"', False),
                                      ('"ram\\u0000extra"', False), ('"flash "', False),
                                      ('0', False), ('true', False), ('null', False), ('{}', False)]:
                run([str(binary), expression, str(int(valid)), 'normal'])
            for scenario in ('arity-zero', 'arity-two', 'sdk-error'):
                run([str(binary), '"flash"', '0', scenario])


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_storage/boundaries.inc')

RESET = fixture_text('wifi/driver/test_wifi_driver_storage/reset.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_storage/main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_storage/vm_main.inc')
