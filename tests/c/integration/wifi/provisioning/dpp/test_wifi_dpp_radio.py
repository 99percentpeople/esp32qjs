"""Deferred production DPP Radio lease/channel/cleanup admission cases."""
from tests.support.fixtures import fixture_text
import os
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code, COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types, structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, extract


class DppRadio(unittest.TestCase):
    def test_exact_owners_capture_retirement_and_channel_restore(self):
        if not os.environ.get('IDF_PATH'):
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = '#define CONFIG_ESP_WIFI_DPP_SUPPORT 1\n' + vendor_code('esp32c3/representative')
        before = sdk_types('esp32c3/representative')
        more = sdk_types('esp32c3/representative', ('wifi_ap_record_t', 'wifi_second_chan_t', 'wifi_ps_type_t'))
        assert more.startswith(before)
        extra = more[len(before):] + TYPES
        extra += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        extra += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        for name in ('connection', 'result', 'worker', 'radio'):
            extra += unit(COMPONENT / 'internal' / f'esp32_mquickjs_wifi_dpp_{name}.h')
        extra += structure(radio, 'wifi_radio_dpp_t') + GLOBALS
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,next_lifecycle_identity,wake_locks;',
            'generation,next_operation_identity,next_lease_identity,next_lifecycle_identity,wake_locks;')
        code = code.replace('bool driver_owned,', fixture_text('wifi/provisioning/dpp/test_wifi_dpp_radio/test_exact_owners_capture_retirement_and_channel_restore-code.inc'), 1)
        code += BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault', 'wifi_radio_config_equal',
                     'esp32_mquickjs_wifi_radio_pmf_disable_allowed',
                     'wifi_radio_restore_disabled_pmf', 'wifi_radio_connection_owner_locked',
                     'esp32_mquickjs_wifi_radio_end_operation', 'wifi_radio_stop_owners_locked'):
            code += extract(radio, name)
        code += (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_dpp_radio.inc').read_text()
        compile_run(self, code + CASES)


TYPES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_radio/types.inc')
GLOBALS = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_radio/globals.inc')
BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_radio/boundaries.inc')
CASES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_radio/cases.inc')
