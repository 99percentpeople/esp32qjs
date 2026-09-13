"""Deferred production event-mask control: preserve control events and rollback safely."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def mask_code(profile, ap=True):
    code = control_code(profile, ap) + BOUNDARIES
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    return code + extract(source, 'esp32_mquickjs_wifi_radio_event_mask') + RESET


class WiFiEventMask(unittest.TestCase):
    def test_real_mask_reserved_bits_readback_rollback_and_faults(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, mask_code(profile, ap) + MAIN)

    def test_public_strict_mask_arity_original_errors_and_gc(self):
        code = re.sub(r'\bcalls\b', 'native_calls', mask_code('esp32c5/representative'))
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for name in ('driver_read_error', 'driver_phy_write_error', 'js_wifi_driver_get_event_mask', 'js_wifi_driver_set_event_mask'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in [('0', True), ('1', True), ('2', False), ('4294967295', False),
                                      ('-1', False), ('0.5', False), ('"1"', False), ('true', False), ('null', False), ('NaN', False)]:
                run([str(binary), expression, str(int(valid)), 'set'])
            for scenario in ('get', 'get-error', 'set-error', 'set-arity', 'get-arity'):
                run([str(binary), '0', '0' if scenario != 'get' else '1', scenario])


BOUNDARIES = fixture_text('wifi/config/test_wifi_event_mask/boundaries.inc')

RESET = r'''
static void mask_reset(void) {reset();native_mask=1;corrupt_mask_readback=corrupt_mask_rollback=false;unsafe_writes=0;}
'''

MAIN = fixture_text('wifi/config/test_wifi_event_mask/main.inc')

VM_MAIN = fixture_text('wifi/config/test_wifi_event_mask/vm_main.inc')
