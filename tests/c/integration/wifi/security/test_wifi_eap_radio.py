"""Deferred production EAP Radio pins and runtime disconnect/retirement gates.

The installer is an injected boundary here; its own fixture uses production
profile/transaction code. Radio registry, admission, release, EAP commands and
runtime prepare bodies are production source, not a parallel ownership model.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code, COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


class WiFiEAPRadio(unittest.TestCase):
    def code(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        code = vendor_code('esp32c5/representative', enterprise=True)
        before = sdk_types('esp32c5/representative')
        extra = sdk_types('esp32c5/representative', ('wifi_ap_record_t',))
        assert extra.startswith(before)
        code += extra[len(before):]
        for name in ('esp32_mquickjs_wifi_enterprise_profile.h', 'esp32_mquickjs_wifi_eap_sdk.h', 'esp32_mquickjs_wifi_eap_install.h', 'esp32_mquickjs_wifi_eap_config.h'):
            code += unit(COMPONENT / 'internal' / name)
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_connection_owner_locked','wifi_radio_eap_exact_locked',
                     'wifi_radio_eap_disconnected_locked','esp32_mquickjs_wifi_radio_eap_install',
                     'esp32_mquickjs_wifi_radio_eap_begin_stop', 'wifi_radio_eap_clear_locked',
                     'esp32_mquickjs_wifi_radio_eap_clear','esp32_mquickjs_wifi_radio_eap_clear_lifecycle',
                     'esp32_mquickjs_wifi_radio_eap_status',
                     'esp32_mquickjs_wifi_radio_eap_identity'):
            code += extract(radio, name)
        code += extract(wifi, 'esp32_mquickjs_wifi_eap_prepare_runtime_destroy')
        return code

    def test_pins_failed_install_exact_clear_and_runtime_retry(self):
        compile_run(self, self.code() + MAIN)

    def test_restart_exact_source_admission_and_cross_generation_failed_install_clear(self):
        code = self.code()
        code = code.replace('int lock;uint32_t generation,',
            'struct {const char *stage;int error;bool mutation_attempted;} configuration;\n'
            'unsigned event_phase,event_live;int lock;uint32_t generation,')
        code = code.replace('owner;bool uncertain;} s_interval;', 'owner;bool uncertain,restore_pending;} s_interval;')
        code = code.replace('unsigned identity,generation;} s_tx_rate_lease;',
                            'unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
        code += RESTART_BOUNDARIES
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for name in ('esp32_mquickjs_wifi_radio_eap_begin_restart', 'wifi_radio_eap_restart_install_locked'):
            code += extract(radio, name)
        compile_run(self, code + RESTART_MAIN)


BOUNDARIES = fixture_text('wifi/security/test_wifi_eap_radio/boundaries.inc')

RESTART_BOUNDARIES = fixture_text('wifi/security/test_wifi_eap_radio/restart_boundaries.inc')

RESTART_MAIN = fixture_text('wifi/security/test_wifi_eap_radio/restart_main.inc')

MAIN = fixture_text('wifi/security/test_wifi_eap_radio/main.inc')
