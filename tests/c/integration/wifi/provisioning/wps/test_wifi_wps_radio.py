"""Deferred production WPS Radio restoration/admission checks; AST only this wave.

The actual Radio functions, lease release, semantic config comparison and secure
zero helper run with controllable driver/worker/event boundaries. This does not
prove native driver normalization, Flash persistence, RF or Station helper drain.
"""
from tests.support.fixtures import fixture_text
import os
from pathlib import Path
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code, COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


class WPSRadio(unittest.TestCase):
    def test_production_restore_suffix_and_exact_owners(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API 1\n#define CONFIG_LWIP_IPV4 1\n' + vendor_code('esp32c5/representative')
        before = sdk_types('esp32c5/representative')
        more = sdk_types('esp32c5/representative', ('wifi_ap_record_t', 'wifi_second_chan_t'))
        assert more.startswith(before)
        extra = more[len(before):] + TYPES
        extra += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        extra += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        extra += unit(Path(sdk) / 'components/wpa_supplicant/esp_supplicant/include/esp_wps.h')
        for name in ('sdk', 'worker', 'radio'):
            extra += unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_wps_' + name + '.h'))
        extra += structure(radio, 'wifi_radio_wps_t') + GLOBALS
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,next_lifecycle_identity,wake_locks;', 'generation,next_operation_identity,next_lease_identity,next_lifecycle_identity,wake_locks;')
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_wps_observe_fence', 'wifi_radio_connection_owner_locked',
                     'wifi_radio_config_equal', 'esp32_mquickjs_wifi_radio_end_operation'):
            code += extract(radio, name)
        code += '\n#define calloc binding_calloc\n#define free binding_free\n'
        code += (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_wps_radio.inc').read_text()
        code += '\n#undef calloc\n#undef free\n'
        compile_run(self, code + CASES)


TYPES = fixture_text('wifi/provisioning/wps/test_wifi_wps_radio/types.inc')

GLOBALS = fixture_text('wifi/provisioning/wps/test_wifi_wps_radio/globals.inc')

BOUNDARIES = fixture_text('wifi/provisioning/wps/test_wifi_wps_radio/boundaries.inc')

CASES = fixture_text('wifi/provisioning/wps/test_wifi_wps_radio/cases.inc')
