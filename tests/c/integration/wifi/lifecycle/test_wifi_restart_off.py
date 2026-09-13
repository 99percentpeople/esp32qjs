"""Deferred off-source capture and final STOP/off/storage production suffix.

Uses actual capture/replay/commit and final off coordinator. Physical generation,
SDK calls, START lease delivery and STOP events are injected boundaries. The
shared actual resume ordering is covered in test_wifi_policy_replay; these
fixtures do not establish SDK normalization, RF, NVS or scheduler correctness.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.c.integration.wifi.config.test_wifi_config_controls import RADIO
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiRestartOff(unittest.TestCase):
    def test_off_capture_replay_final_stop_storage_and_every_sdk_failure(self):
        helpers = CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    code = config_code(profile, ap, mutation_boundary=True)
                    getter = 'static int esp_wifi_get_mode(wifi_mode_t *mode) {*mode=native_mode;return sdk_step(false);}'
                    self.assertEqual(code.count(getter), 1)
                    code = code.replace(getter, 'static bool off_readback_corrupt;\n' +
                        'static int esp_wifi_get_mode(wifi_mode_t *mode) {'
                        '*mode=off_readback_corrupt && native_mode==WIFI_MODE_NULL ? WIFI_MODE_STA : native_mode;'
                        'return sdk_step(false);}')
                    code += BOUNDARIES
                    code += extract(RADIO.read_text(), 'esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle')
                    compile_run(self, code + helpers + MAIN)


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_restart_off/boundaries.inc')


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_off/main.inc')
