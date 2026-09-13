"""Deferred real restart capture/binding/error conversion with native boundaries.

Native execution and wait scopes are injected here. The production Radio gate,
runtime executor, phase sequence and actual wait budget have separate fixtures;
this checks JS admission, dispatch-once, diagnostics and GC/OOM, not reconstruction.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest

from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiPublicRestart(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi.h').read_text()
        radio = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        config = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        code = 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 2\n'
        code += structure(header, 'esp32_mquickjs_wifi_configuration_execution_t')
        code += structure(radio, 'esp32_mquickjs_wifi_radio_config_result_t')
        code += unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        code += BOUNDARIES
        code += ''.join(extract(config, name) for name in (
            'esp32_mquickjs_wifi_capture_restart',))
        code += ''.join(extract(wifi, name) for name in (
            'wifi_make_configuration_status', 'wifi_throw_restart_error', 'js_wifi_driver_restart'))
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_strict_input_default_budget_and_nth_vm_allocation(self):
        for expression, expected in [
            ('undefined', 10000), ('({})', 10000), ('({timeoutMs:undefined})', 10000),
            ('({allowApRestart:undefined})', 10000), ('({allowApRestart:false})', 10000),
            ('({allowApRestart:1})', 0), ('({allowApRestart:null})', 0), ('({allowApRestart:"true"})', 0),
            ('({timeoutMs:1})', 1), ('({timeoutMs:60000})', 60000), ('({timeoutMs:60001})', 60001), ('({timeoutMs:2147483647})', 2147483647),
            ('null', 0), ('[]', 0), ('({timeoutMs:0})', 0), ('({timeoutMs:-1})', 0),
            ('({timeoutMs:2147483648})', 0), ('({timeoutMs:1.5})', 0), ('({timeoutMs:"10"})', 0),
            ('({timeoutMs:4294967297})', 0), ('({timeoutMs:0/0})', 0), ('({timeoutMs:1/0})', 0),
            ('({"timeoutMs\\u0000":10})', 0), ('({force:true})', 0),
            ('({requireExclusive:false})', 0), ('({mode:"station"})', 0)]:
            with self.subTest(expression=expression):
                run([str(self.binary), expression, str(expected), '0'])

    def test_explicit_ap_option_survives_gc_and_nth_allocation(self):
        run([str(self.binary), '({timeoutMs:100,allowApRestart:true})', '100', '6'])

    def test_wait_rejection_admission_failure_current_call_details_and_gc(self):
        for scenario in (1, 2, 3, 4, 5, 7):
            run([str(self.binary), '({timeoutMs:100})', '100', str(scenario)])


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_public_restart/boundaries.inc')


MAIN = fixture_text('wifi/lifecycle/test_wifi_public_restart/main.inc')
