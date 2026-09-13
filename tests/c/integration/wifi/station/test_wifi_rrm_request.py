"""Deferred native RRM/Radio lifecycle; no fixture execution in the API wave.

Compile production request/registry/Radio bodies; inject only SDK callback
storage, dispatch, worker queue, allocation, clock and locks. The SDK source
itself has separate original/patched coverage in test_idf_rrm.py.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code, COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


class WiFiRRMRequest(unittest.TestCase):
    def test_production_radio_identity_callback_storage_and_retirement(self):
        internal = COMPONENT / 'internal'
        header = (internal / 'esp32_mquickjs_wifi_radio.h').read_text()
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = '#include <stdint.h>\n#include <stdlib.h>\n#define CONFIG_ESP_WIFI_RRM_SUPPORT 1\n#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        code += '#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        code += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        injected = vendor_code('esp32c5/representative')
        injected = injected.replace('struct {unsigned identity,lease_identity;} operation;',
                                    'esp32_mquickjs_wifi_radio_operation_t operation; uint32_t next_operation_identity;')
        code += injected
        code += unit(internal / 'esp32_mquickjs_wifi_rrm_sdk.h')
        code += unit(internal / 'esp32_mquickjs_wifi_rrm_radio.h')
        code += unit(internal / 'esp32_mquickjs_wifi_rrm_request.h')
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ['wifi_radio_connection_owner_locked', 'wifi_radio_rrm_exact_locked',
                     'esp32_mquickjs_wifi_radio_rrm_reserve', 'esp32_mquickjs_wifi_radio_rrm_command',
                     'esp32_mquickjs_wifi_radio_rrm_retire', 'esp32_mquickjs_wifi_radio_end_operation']:
            code += extract(radio, name)
        code += RESERVE_BRIDGE + COPY_BOUNDARY
        code += unit(COMPONENT / 'src/modules/wifi_roaming/esp32_mquickjs_wifi_rrm_request.c')
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/station/test_wifi_rrm_request/boundaries.inc')
RESERVE_BRIDGE = fixture_text('wifi/station/test_wifi_rrm_request/reserve_bridge.inc')
COPY_BOUNDARY = fixture_text('wifi/station/test_wifi_rrm_request/copy_boundary.inc')
MAIN = fixture_text('wifi/station/test_wifi_rrm_request/main.inc')
