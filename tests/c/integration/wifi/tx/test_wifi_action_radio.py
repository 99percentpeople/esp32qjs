"""Deferred real Radio Action admission, SDK boundary, event/fence and lease retirement.

Reuses production registry fixture. SDK storage/calls, event-loop post scheduling
and driver-queue fence are injected, not substitutes for the production controller.
No runtime Future, actual SDK-task scheduling or RF proof is implied.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def radio_code(profile, ap):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code = vendor_code(profile, ap)
    code = code.replace('struct {unsigned identity,lease_identity;} operation;',
                        'esp32_mquickjs_wifi_radio_operation_t operation;uint32_t next_operation_identity;'
                        'unsigned event_identity,event_revision,event_phase;bool event_fence_posted,event_fence_seen;')
    types = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
    types += re.search(r'typedef struct \{[^}]*\} esp32_mquickjs_wifi_radio_operation_t;', header).group(0)
    index = code.index('static struct {')
    code = code[:index] + types + code[index:]
    extra = ('wifi_action_tx_req_t', 'wifi_roc_req_t', 'wifi_event_action_tx_status_t',
             'wifi_event_roc_done_t', 'wifi_ap_record_t')
    declarations = sdk_types(profile, extra)
    index = code.index('static struct {')
    code = code[:index] + declarations[len(sdk_types(profile)):] + code[index:]
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_action_lane.h')
    code += unit(COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_lane.c')
    code += re.search(r'static struct \{\n    esp32_mquickjs_wifi_action_lane_t[^}]*\} s_action = [^;]*;', radio).group(0)
    for name in ('wifi_radio_action_fence_t', 'wifi_radio_event_fence_t'):
        code += re.search(r'typedef struct \{[^}]*\} '+name+';', radio).group(0)
    code += BOUNDARIES
    for name in ('wifi_radio_lifecycle_fence', 'wifi_radio_action_event', 'wifi_radio_action_exact_locked',
                 'wifi_radio_action_parameters', 'wifi_radio_action_admit_locked',
                 'esp32_mquickjs_wifi_radio_action_send', 'esp32_mquickjs_wifi_radio_action_roc',
                 'esp32_mquickjs_wifi_radio_action_cancel', 'esp32_mquickjs_wifi_radio_action_status',
                 'esp32_mquickjs_wifi_radio_action_retire', 'esp32_mquickjs_wifi_radio_end_operation'):
        code += extract(radio, name)
    return code


class WiFiActionRadio(unittest.TestCase):
    def test_radio_admission_native_completion_fences_cancel_and_owner_retention(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, radio_code(profile, ap) + MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_action_radio/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_action_radio/main.inc')
