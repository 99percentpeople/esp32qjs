"""Deferred actual AP rate Radio startup/cleanup; not imported/run in API phase.

Composes production registry, rate transaction, STOP/restore, lifecycle admission
and AP start/cleanup. SDK state, helper readiness, validation result and allocation
are injected boundaries. No substitute startup/cleanup state machine.
"""
from tests.support.fixtures import fixture_text
import json
import unittest
from tests.c.integration.wifi.tx.test_wifi_raw_tx_rate_lease import radio_rate_code
from tests.c.integration.wifi.tx.test_wifi_tx_rate import COMPONENT, ROOT
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def ap_rate_code(profile):
    code = radio_rate_code(profile)
    extra = sdk_types(profile)
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    for value in symbols.values():
        decl = value.get('declaration', '')
        if decl.startswith('typedef ') and decl in code:
            extra = extra.replace(decl + ';', '')
    code = code.replace('#define WIFI_RADIO_MAX_LEASES 4',
                        extra + '\n#define WIFI_RADIO_MAX_LEASES 4')
    code = code.replace('unsigned generation,next_lease_identity,clients',
                        'unsigned generation,next_lease_identity,next_lifecycle_identity,clients')
    code = code.replace('struct {uint32_t identity,lease_identity;} operation,lifecycle;',
                        'struct {uint32_t identity,lease_identity;} operation;\n'
                        'esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;')
    code = code.replace('s_radio.effective_mode=WIFI_MODE_STA;', 'assert(s_radio.leases[0].required_mode==s_radio.effective_mode);')
    code = code.replace('interface==WIFI_IF_STA && s_tx_rate_lease.identity',
                        'interface==WIFI_IF_AP && s_tx_rate_lease.identity')
    code += BOUNDARIES
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    for name in ('wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
                 'wifi_radio_check_stopped_lifecycle_locked', 'esp32_mquickjs_wifi_radio_check_stopped_lifecycle',
                 'wifi_radio_config_equal', 'wifi_radio_validate_saved_ap_config',
                 'esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration',
                 'esp32_mquickjs_wifi_radio_begin_raw_tx_ap_rate',
                 'esp32_mquickjs_wifi_radio_start_raw_tx_ap_rate',
                 'esp32_mquickjs_wifi_radio_quiesce_raw_tx_ap_rate'):
        code += extract(radio, name)
    return code


class WiFiRawTxApRate(unittest.TestCase):
    def test_saved_wpa2_sdk_sae_default_is_preserved_without_weakening_input(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for profile in ('esp32c3/representative','esp32s3/representative-psram','esp32c5/representative'):
            with self.subTest(profile=profile):
                code = ap_rate_code(profile)
                original = extract(code,'esp32_mquickjs_wifi_radio_validate_ap_config')
                code = code.replace(original, fixture_text('wifi/tx/test_wifi_raw_tx_ap_rate/test_saved_wpa2_sdk_sae_default_is_preserved_without_weakening_input-code.inc') + extract(radio,'esp32_mquickjs_wifi_radio_validate_ap_config'))
                compile_run(self,code+MAIN[:MAIN.index('int main(void)')]+fixture_text('wifi/tx/test_wifi_raw_tx_ap_rate/test_saved_wpa2_sdk_sae_default_is_preserved_without_weakening_input.inc'))

    def test_start_snapshot_failure_handoff_and_atomic_cleanup(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, ap_rate_code(profile) + MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_ap_rate/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_ap_rate/main.inc')
