"""Deferred production pre-start Vendor IE transfer and resume/cleanup ordering.

Uses the real slot operations, registry, start admission, initialize/configure/
resume/finish entry points. Physical START/STOP, helper preparation and general
configuration/policy callbacks are injected boundaries, not RF/runtime proof.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def prestart_code(profile, ap):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    declarations = ''.join(structure(header, n) for n in (
        'esp32_mquickjs_wifi_radio_config_result_t', 'esp32_mquickjs_wifi_radio_config_controls_t',
        'esp32_mquickjs_wifi_radio_start_controls_t'))
    code = vendor_code(profile, ap)
    code = code.replace('static struct {\n    int lock;', declarations + 'static struct {\n    int lock;', 1)
    code = code.replace('    wifi_mode_t effective_mode;wifi_storage_t storage;',
                        '    wifi_mode_t effective_mode;wifi_storage_t storage;esp32_mquickjs_wifi_radio_config_result_t configuration;')
    code += BOUNDARIES
    for name in ('wifi_radio_record_fault', 'esp32_mquickjs_wifi_radio_begin_start_lifecycle',
                 'esp32_mquickjs_wifi_radio_initialize_lifecycle', 'esp32_mquickjs_wifi_radio_configure_lifecycle',
                 'wifi_radio_resume_lifecycle_locked', 'esp32_mquickjs_wifi_radio_resume_lifecycle',
                 'esp32_mquickjs_wifi_radio_finish_lifecycle'):
        code += extract(radio, name)
    return code


class WiFiVendorIePrestart(unittest.TestCase):
    def test_handoff_copy_preservation_exact_token_first_start_owners_and_cleanup_failures(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(target=profile, softap=ap):
                    compile_run(self, prestart_code(profile, ap) + MAIN)


BOUNDARIES = fixture_text('wifi/config/test_wifi_vendor_ie_prestart/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_vendor_ie_prestart/main.inc')
