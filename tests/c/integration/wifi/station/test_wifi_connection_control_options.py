"""Deferred real public controls + coordinator/Radio with SDK boundaries and GC."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiConnectionControlOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code = control_code('esp32c5/representative')
        code = re.sub(r'\bcalls\b', 'native_calls', code)
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('tx_rate_interface', 'driver_phy_write_error', 'driver_connection_control',
                     'js_wifi_driver_set_inactive_time', 'js_wifi_driver_set_rssi_threshold'):
            code += extract(driver, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_input_bounds_arity_original_native_errors_and_error_gc(self):
        cases = [
            ('inactive', '["station",3]', True), ('inactive', '["station",65535]', True),
            ('inactive', '["access-point",10]', True), ('inactive', '["access-point",9]', False),
            ('inactive', '["station",2]', False), ('inactive', '["station",65536]', False),
            ('inactive', '["station",3.5]', False), ('inactive', '["station","3"]', False),
            ('inactive', '["station\\u0000",3]', False), ('inactive', '["station",3,4]', False),
            ('rssi', '[-100]', True), ('rssi', '[10]', True), ('rssi', '[0]', True),
            ('rssi', '[-101]', False), ('rssi', '[11]', False), ('rssi', '[-20.5]', False),
            ('rssi', '["-60"]', False), ('rssi', '[true]', False), ('rssi', '[]', False),
            ('rssi', '[-60,-70]', False), ('rssi', '[4294967295]', False),
        ]
        for kind, expression, expected in cases:
            with self.subTest(kind=kind, expression=expression):
                run([str(self.binary), kind, expression, str(int(expected)), '0'])
        for kind, expression in [('inactive', '["station",3]'), ('rssi', '[-75]')]:
            run([str(self.binary), kind, expression, '0', '1'])


MAIN = fixture_text('wifi/station/test_wifi_connection_control_options/main.inc')
