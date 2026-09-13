"""Deferred production Driver observations, SDK failures, public arity and VM GC."""
from tests.support.fixtures import fixture_text
import json
import re
import tempfile
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT, ROOT, phy_types
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def observe_code(profile, ap=True):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {key.split('::')[-1]: item['declaration'] for key, item in symbols.items()}
    code = phy_types(profile, ap)
    code += f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(profile.startswith("esp32c5/"))}\n'
    for name in ('wifi_band_t', 'wifi_ps_type_t', 'wifi_phy_mode_t'):
        code += declarations[name] + ';\n'
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_driver_query_t;', header).group(0)
    rates = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c').read_text()
    code += extract(rates, 'esp32_mquickjs_wifi_tx_phy_name') + BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    return code + extract(radio, 'esp32_mquickjs_wifi_radio_read_driver')


class WiFiDriverObserve(unittest.TestCase):
    def test_native_sdk_error_decode_range_and_stable_state_admission(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, observe_code(profile, ap) + NATIVE_MAIN)

    def test_public_arity_sdk_error_details_scalar_values_gc_and_oom(self):
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        core = (CORE / 'esp32_mquickjs.c').read_text()
        code = observe_code('esp32c5/representative') + extract(core, 'esp32_mquickjs_throw_native_error')
        for name in ('tx_rate_interface', 'driver_read_error', 'driver_scalar_to_js', 'driver_read_scalar',
                     'js_wifi_driver_get_band', 'js_wifi_driver_get_band_mode', 'js_wifi_driver_get_power_save',
                     'js_wifi_driver_get_tx_power', 'js_wifi_driver_get_rssi', 'js_wifi_driver_get_aid',
                     'js_wifi_driver_get_negotiated_phy', 'js_wifi_driver_get_tsf_time', 'js_wifi_driver_get_inactive_time'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for query in range(9):
                for scenario in ('success', 'sdk-error', 'arity'):
                    run([str(binary), str(query), scenario])


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_observe/boundaries.inc')

NATIVE_MAIN = fixture_text('wifi/driver/test_wifi_driver_observe/native_main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_observe/vm_main.inc')
