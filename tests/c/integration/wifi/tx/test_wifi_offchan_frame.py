"""Deferred production off-channel frame provenance and address reuse checks.

The production C helper is compiled with controlled SDK output/reset/recycle
boundaries when the Wi-Fi test wave runs. Do not run during implementation.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_offchan_frame.c'


class OffchanFrameIdentity(unittest.TestCase):
    def test_actual_frame_reset_recycle_and_synchronous_output(self):
        source = re.sub(r'^#include[^\n]*\n', '', SOURCE.read_text(), flags=re.M)
        header = re.sub(r'^#(?:include|pragma once).*\n', '',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_offchan_frame.h').read_text(), flags=re.M)
        for c5 in (0, 1):
            with self.subTest(c5=c5):
                compile_run(self, BOUNDARIES.replace('#define CONFIG_IDF_TARGET_ESP32C5 0',
                    f'#define CONFIG_IDF_TARGET_ESP32C5 {c5}') + header + source + MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_offchan_frame/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_offchan_frame/main.inc')
