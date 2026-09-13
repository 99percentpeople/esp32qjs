"""Deferred production SDK boolean policies; acceptance is not readback/recovery."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def policy_code(profile, ap=True, coex=False):
    code = f'#define CONFIG_ESP_COEX_POWER_MANAGEMENT {int(coex)}\n' + control_code(profile, ap)
    code = code.replace('wifi_mode_t effective_mode;', 'wifi_mode_t effective_mode;bool stop_required;')
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_policy_control_t;', header).group(0)
    code += '\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_policy.h')
    code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c')
    code += 'static esp32_mquickjs_wifi_policy_state_t s_policies;\n'
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    return code + extract(radio, 'wifi_radio_policy_writer') + extract(radio, 'esp32_mquickjs_wifi_radio_policy_status') + extract(radio, 'esp32_mquickjs_wifi_radio_write_policy') + extract(wifi, 'esp32_mquickjs_wifi_apply_policy') + RESET


class WiFiDriverPolicy(unittest.TestCase):
    def test_native_state_owner_feature_gates_and_uncertain_failure(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                for coex in (False, True):
                    with self.subTest(profile=profile, ap=ap, coex=coex):
                        compile_run(self, policy_code(profile, ap, coex) + MAIN)

    def test_public_boolean_interface_arity_errors_and_moving_gc(self):
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        for coex in (False, True):
            code = re.sub(r'\bcalls\b', 'native_calls', policy_code('esp32c5/representative', True, coex))
            code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
            code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
            for name in ('tx_rate_interface', 'driver_phy_write_error', 'driver_write_policy',
                         'js_wifi_driver_set_dynamic_carrier_sense', 'js_wifi_driver_configure_11b_rate',
                         'js_wifi_driver_set_coexistence_power_management'):
                code += extract(driver, name)
            with tempfile.TemporaryDirectory() as tmp:
                binary = build(tmp, code, VM_MAIN)
                for query in range(3):
                    for scenario in ('true', 'false', '1', '"true"', 'null', 'arity', 'sdk-error', 'interface'):
                        run([str(binary), str(query), scenario])


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_policy/boundaries.inc')

RESET = fixture_text('wifi/driver/test_wifi_driver_policy/reset.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_policy/main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_policy/vm_main.inc')
