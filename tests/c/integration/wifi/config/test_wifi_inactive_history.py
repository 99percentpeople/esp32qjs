"""Deferred production AP retirement observation and hidden-value checkpoint.

Uses actual lease validation/AP-stop/capture/replay helpers; SDK/event fences,
physical rebuild/start and native storage are injected. No RF/NVS proof.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def history_code(profile):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = config_code(profile, True, mutation_boundary=True)
    code = code.replace('wifi_mode_t effective_mode;',
        'wifi_mode_t effective_mode;unsigned ap_stop_phase;esp_err_t ap_stop_error;'
        'uint32_t ap_transition_application,ap_transition_station,ap_transition_access_point;')
    old = extract(code, 'wifi_radio_begin_events')
    code = code.replace(old, 'static bool ap_close_events;\n' + old.replace(
        '    assert((phase==RADIO_EVENTS_START',
        '    if(phase==RADIO_EVENTS_AP_STOP){assert(mode==WIFI_MODE_AP && s_radio.started);ap_close_events=true;return sdk_step(false);}\n'
        '    assert((phase==RADIO_EVENTS_START', 1))
    old = extract(code, 'wifi_radio_wait_events')
    code = code.replace(old, old.replace('    assert(s_radio.started',
        '    if(ap_close_events){int err=sdk_step(false);if(err==ESP_OK)ap_close_events=false;return err;}\n'
        '    assert(s_radio.started', 1))
    old = extract(code, 'esp_wifi_set_mode')
    code = code.replace(old, old.replace('assert(!s_radio.started);',
        'assert(!s_radio.started || (native_mode==WIFI_MODE_APSTA && mode==WIFI_MODE_STA));'))
    code += re.search(r'enum \{ AP_STOP_IDLE[^}]*\};', radio).group(0)
    for name in ('wifi_radio_ap_transition_valid', 'wifi_radio_ap_stop_valid',
                 'esp32_mquickjs_wifi_radio_quiesce_ap_lifecycle'):
        code += extract(radio, name)
    return code + CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]


class WiFiInactiveHistory(unittest.TestCase):
    def test_production_ap_stop_history_capture_and_physical_generation(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, history_code(profile) + MAIN)


MAIN = fixture_text('wifi/config/test_wifi_inactive_history/main.inc')
