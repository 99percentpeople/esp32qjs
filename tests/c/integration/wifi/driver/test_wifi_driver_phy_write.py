"""Deferred production PHY setter transaction with real shared config helpers."""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import RADIO, HEADER, PRELUDE, BOUNDARIES, sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiDriverPhyWrite(unittest.TestCase):
    def test_atomic_admission_readback_partial_failure_rollback_and_band_selection(self):
        source, header = RADIO.read_text(), HEADER.read_text()
        local = '\n'.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lifecycle_t', 'esp32_mquickjs_wifi_radio_config_result_t',
            'esp32_mquickjs_wifi_radio_config_controls_t', 'esp32_mquickjs_wifi_phy_readback_t'))
        local += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_phy_query_t;', header).group(0)
        local += structure(source, 'wifi_radio_controls_snapshot_t')
        boundaries = BOUNDARIES.replace('bool driver_owned,storage_configured,started,stop_required,promiscuous_claimed;',
                                        'bool driver_owned,storage_configured,started,stop_required,promiscuous_claimed,restart_required;')
        boundaries += '\nstatic struct {unsigned identity;} s_tx_rate_lease;\n'
        functions = ('esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_country_equal',
                     'wifi_radio_validate_protocol', 'esp32_mquickjs_wifi_radio_validate_config_controls',
                     'wifi_radio_read_phy', 'wifi_radio_phy_equal', 'wifi_radio_write_phy',
                     'wifi_radio_snapshot_controls', 'wifi_radio_apply_controls', 'wifi_radio_restore_controls',
                     'esp32_mquickjs_wifi_radio_write_phy')
        production = ''.join(extract(source, name) for name in functions)
        for profile, five, he in [('esp32c3/representative', 0, 0), ('esp32s3/representative-psram', 0, 0), ('esp32c5/representative', 1, 1)]:
            for ap in (0, 1):
                with self.subTest(profile=profile, ap=ap):
                    gates = (f'#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n'
                             f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n#define ESP_ERR_WIFI_NOT_INIT -9\n')
                    compile_run(self, PRELUDE + gates + sdk_types(profile) + local + boundaries + production + MAIN)


MAIN = fixture_text('wifi/driver/test_wifi_driver_phy_write/main.inc')
