"""Deferred production FTM Radio admission/report retirement schedule.

Extracts actual production functions and registry. Only SDK calls, timer/event
scheduling and lock/storage boundaries are injected. Does not model another FSM,
prove the SDK timer/IPC implementation, or execute the future public JS adapter.
This file is written and AST parsed only until the Wi-Fi stage test gate opens.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_radio import radio_code
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def ftm_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = re.sub(r'\bowners\(', 'action_fixture_owners(', radio_code(profile, ap))
    extra = sdk_types(profile, ('wifi_ftm_initiator_cfg_t', 'wifi_event_ftm_report_t', 'wifi_ftm_report_entry_t'))
    declarations = extra[len(sdk_types(profile)):]
    declarations += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_ftm_radio.h')
    declarations += TIMER_TYPES
    for pattern in (r'static struct \{\n    esp32_mquickjs_wifi_ftm_state_t[^}]*\} s_ftm;',
                    r'static struct \{\n    esp_timer_handle_t[^}]*\} s_ftm_timer_fence;',
                    r'typedef struct \{[^}]*\} wifi_radio_ftm_fence_t;'):
        declarations += re.search(pattern, source).group(0) + '\n'
    index = code.index('static struct {\n    esp32_mquickjs_wifi_action_lane_t')
    code = code[:index] + declarations + code[index:]
    code = '#define CONFIG_ESP_WIFI_FTM_ENABLE 1\n#define CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT 1\n' + code
    code += BOUNDARIES
    for name in ('wifi_radio_ftm_event', 'wifi_radio_ftm_exact_locked', 'wifi_radio_ftm_recovery_exact_locked', 'wifi_radio_ftm_admit_locked',
                 'esp32_mquickjs_wifi_ftm_config_valid', 'esp32_mquickjs_wifi_radio_ftm_start', 'esp32_mquickjs_wifi_radio_ftm_end',
                 'esp32_mquickjs_wifi_radio_ftm_snapshot', 'esp32_mquickjs_wifi_radio_ftm_status',
                 'wifi_radio_ftm_timer_fence_callback', 'wifi_radio_ftm_timer_fence_locked',
                 'wifi_radio_ftm_fence_locked', 'esp32_mquickjs_wifi_radio_ftm_collect',
                 'esp32_mquickjs_wifi_radio_ftm_retire'):
        code += extract(source, name)
    return code


class WiFiFtmRadio(unittest.TestCase):
    def test_exact_owner_report_transfer_and_control_fences(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, ftm_code(profile, ap) + MAIN)


TIMER_TYPES = fixture_text('wifi/ftm/test_wifi_ftm_radio/timer_types.inc')

BOUNDARIES = fixture_text('wifi/ftm/test_wifi_ftm_radio/boundaries.inc')

MAIN = fixture_text('wifi/ftm/test_wifi_ftm_radio/main.inc')
