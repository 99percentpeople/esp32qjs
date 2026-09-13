"""Deferred production PMF mutation and preservation; injected SDK/VM only."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest

from tests.c.integration.wifi.driver.test_wifi_driver_storage import storage_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def pmf_code(profile, ap=True):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = storage_code(profile, ap) + BOUNDARIES
    code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
    for name in ('wifi_radio_config_equal', 'esp32_mquickjs_wifi_radio_pmf_disable_allowed',
                 'wifi_radio_restore_disabled_pmf', 'esp32_mquickjs_wifi_radio_disable_pmf'):
        code += extract(source, name)
    return code + RESET


class WiFiDriverPmf(unittest.TestCase):
    def test_production_security_admission_partial_writes_full_readback_and_restore(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, pmf_code(profile, ap) + MAIN)

    def test_public_strict_interface_arity_error_roots_and_no_success_allocation(self):
        code = re.sub(r'\bcalls\b', 'native_calls', pmf_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('tx_rate_interface', 'driver_phy_write_error', 'js_wifi_driver_disable_pmf'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('"station"', True), ('"access-point"', True),
                                      ('"station\\u0000extra"', False), ('"STATION"', False),
                                      ('"ap"', False), ('null', False), ('0', False), ('{}', False)]:
                run([str(binary), expression, str(int(valid)), 'normal'])
            for scenario in ('arity-zero', 'arity-two', 'sdk-error', 'security'):
                run([str(binary), '"station"', '0', scenario])


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_pmf/boundaries.inc')

RESET = fixture_text('wifi/driver/test_wifi_driver_pmf/reset.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_pmf/main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_pmf/vm_main.inc')
