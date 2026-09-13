"""Production selection/admission and semantic Station readback; phase run deferred.

SDK types are recorded declarations. The real Radio mutex boundary is injected,
with a mutation on entry to prove defaults are read after acquisition. No SDK
function is supplied: resolution/admission must not call any driver function.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.support.wireless_vm_fixture import ROOT, extract
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure
from tests.support.native_compile import compile_run

HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h'
RADIO = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'

BOUNDARIES = fixture_text('wifi/config/test_wifi_configuration_selection/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_configuration_selection/main.inc')

class WiFiConfigurationSelection(unittest.TestCase):
    def test_defaults_authorization_exact_owners_and_readback(self):
        header, radio = HEADER.read_text(), RADIO.read_text()
        enums = '\n'.join(re.search(r'typedef enum \{[^}]*\} ' + name + r';', header).group(0)
                          for name in ('esp32_mquickjs_wifi_radio_client_t',))
        structs = '\n'.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_lifecycle_t',
            'esp32_mquickjs_wifi_radio_config_controls_t', 'esp32_mquickjs_wifi_radio_start_controls_t',
            'esp32_mquickjs_wifi_radio_configuration_selection_t'))
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_lease_valid', 'wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
            'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_protocol',
            'esp32_mquickjs_wifi_radio_validate_config_controls', 'esp32_mquickjs_wifi_radio_validate_start_controls',
            'esp32_mquickjs_wifi_radio_begin_configuration_lifecycle', 'wifi_radio_config_equal',
            'esp32_mquickjs_wifi_radio_accept_station_config'))
        for profile, ap, five, he in [('esp32c5',1,1,1),('esp32c3',1,0,0),('esp32c3',0,0,0)]:
            with self.subTest(target=profile, softap=ap):
                defines = (f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {ap}\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n'
                           f'#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n')
                compile_run(self, PRELUDE + defines + sdk_types(profile + '/representative') + enums + structs + BOUNDARIES + functions + MAIN)
