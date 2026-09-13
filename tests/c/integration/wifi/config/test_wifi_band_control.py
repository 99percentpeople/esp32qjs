"""Deferred production band controls; no RF or live-AP switching claim."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def band_code(profile):
    code = control_code(profile, True, ('wifi_band_t', 'wifi_second_chan_t', 'wifi_ap_record_t', 'wifi_phy_mode_t'))
    code = f'#define CONFIG_SOC_WIFI_SUPPORT_5G {int(profile.startswith("esp32c5/"))}\n' + code
    code = code.replace('esp32_mquickjs_wifi_radio_client_t client;} wifi_radio_live_lease_t;',
                        'esp32_mquickjs_wifi_radio_client_t client;bool fixed_channel,channel_conflict;uint32_t raw_tx_identity;} wifi_radio_live_lease_t;')
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    for name in ('esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_regulatory_channel',
                 'wifi_radio_band_snapshot', 'esp32_mquickjs_wifi_radio_change_band'):
        code += extract(radio, name)
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    return code + extract(wifi, 'esp32_mquickjs_wifi_apply_band') + RESET


class WiFiBandControl(unittest.TestCase):
    def test_real_band_admission_sdk_failures_readback_and_no_uncertain_replay(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, band_code(profile) + MAIN)

    def test_public_strict_names_fault_and_result_gc(self):
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = band_code('esp32c5/representative')
        code = re.sub(r'\bcalls\b', 'native_calls', code)
        code = re.sub(r'\bfail_at\b', 'native_fail_at', code)
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_driver_query_t;', header).group(0)
        rate = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c').read_text()
        code += extract(rate, 'esp32_mquickjs_wifi_tx_phy_name') + unit(CORE / 'esp32_mquickjs_options.c')
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_scalar_to_js', 'driver_phy_write_error', 'driver_change_band',
                     'js_wifi_driver_set_band', 'js_wifi_driver_set_band_mode'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for mode, expression, expected in [
                (0, '"5GHz"', True), (0, '"2.4GHz"', True),
                (1, '"2.4GHz-only"', True), (1, '"5GHz-only"', True), (1, '"auto"', True),
                (0, '"5GHz\\u0000"', False), (1, '"auto\\u0000"', False),
                (0, '5', False), (1, 'true', False), (1, '"5GHz"', False), (0, '"5ghz"', False),
            ]:
                run([str(binary), str(mode), expression, str(int(expected)), '0'])
            run([str(binary), '0', '"5GHz"', '0', '1'])


BOUNDARIES = fixture_text('wifi/config/test_wifi_band_control/boundaries.inc')

RESET = fixture_text('wifi/config/test_wifi_band_control/reset.inc')

MAIN = fixture_text('wifi/config/test_wifi_band_control/main.inc')

VM_MAIN = fixture_text('wifi/config/test_wifi_band_control/vm_main.inc')
