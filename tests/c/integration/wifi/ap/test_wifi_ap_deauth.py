"""Deferred real Radio/native deauth transaction; SDK queue and association table injected.

Uses production lease acquire/release, generic operation retirement and the whole
AP deauth include. Does not prove RF delivery or execute fixtures during API work.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code, COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import CORE, extract
from tests.support.native_compile import compile_run


class WiFiAPDeauth(unittest.TestCase):
    def test_current_mac_exact_owner_and_unconfirmed_dispatch_retention(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = vendor_code('esp32c3/representative')
        extra = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        extra += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,', 'event_live,next_operation_identity,generation,next_lease_identity,', 1)
        code += BOUNDARIES
        for path, name in ((CORE / 'esp32_mquickjs_wireless_core.c', 'esp32_mquickjs_wireless_secure_zero'),
                           (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c', 'wifi_radio_record_fault'),
                           (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c', 'esp32_mquickjs_wifi_radio_end_operation')):
            code += extract(path.read_text(), name)
        native = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_ap_deauth.inc').read_text()
        # The real target compiler checks 32-bit ABI layout. Host executes the
        # same control flow with its native pointer width, never driver offsets.
        native = native.replace('_Static_assert(sizeof(wifi_ap_deauth_ipc_t) == 12, "review AP deauth IPC ABI");', '')
        compile_run(self, code + native + MAIN)


BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_deauth/boundaries.inc')

MAIN = fixture_text('wifi/ap/test_wifi_ap_deauth/main.inc')
