"""Production live-STA reopen admission and exact stored config policy; run deferred."""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, RADIO, HEADER, sdk_types, structure
from tests.support.wireless_vm_fixture import ROOT, CORE, extract
from tests.support.native_compile import compile_run


class WiFiAPReopenAdmission(unittest.TestCase):
    def code(self, enabled=False):
        radio, header=RADIO.read_text(),HEADER.read_text()
        client=re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;',header).group(0)
        broker=HEADER.with_name('esp32_mquickjs_wifi_promiscuous_broker.h').read_text()
        structs=structure(broker,'esp32_mquickjs_wifi_promiscuous_token_t')
        structs+=client+structure(header,'esp32_mquickjs_wifi_radio_lease_t')
        structs+=structure(header,'esp32_mquickjs_wifi_radio_lifecycle_t')
        structs+=structure(radio,'wifi_radio_live_lease_t')
        functions=''.join(extract(radio,n) for n in [
            'wifi_radio_lease_valid','wifi_radio_acquire_locked','wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
            'esp32_mquickjs_wifi_radio_5ghz_channel_bit','esp32_mquickjs_wifi_radio_validate_ap_config',
            'esp32_mquickjs_wifi_radio_accept_ap_config','wifi_radio_interfaces_live',
            'wifi_radio_ap_prestart_config_matches','wifi_radio_ap_reopen_config_matches','esp32_mquickjs_wifi_radio_begin_ap_reopen'])
        zero=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        gate = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
        gate += '#define ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE '+str(int(enabled))+'\n'
        return gate+PRELUDE.replace('#define WIFI_RADIO_MAX_LEASES 2','#define WIFI_RADIO_MAX_LEASES 4')+sdk_types('esp32c5/representative')+structs+BOUNDARIES+zero+functions

    def test_different_configuration_capture_and_auto_channel_acceptance(self):
        compile_run(self,self.code(enabled=True)+fixture_text('wifi/ap/test_wifi_ap_reopen_admission/test_different_configuration_capture_and_auto_channel_acceptance.inc'))

    def test_no_mutation_on_config_mismatch_allocation_sdk_or_owner_failures(self):
        compile_run(self,self.code()+fixture_text('wifi/ap/test_wifi_ap_reopen_admission/test_no_mutation_on_config_mismatch_allocation_sdk_or_owner_failures.inc'))


BOUNDARIES = fixture_text('wifi/ap/test_wifi_ap_reopen_admission/boundaries.inc')
