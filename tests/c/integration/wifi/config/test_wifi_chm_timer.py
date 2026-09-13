"""Deferred production CHM deadline/queue/lifetime regression coverage.

Only the SDK timer, native queue and native end boundaries are controlled.
The validation, tickets, deadlines, cancel/reset and cleanup are production C.
Do not import/compile/run during the Wi-Fi implementation wave.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_chm_timer.h'
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_chm_timer.c'


class ChmTimer(unittest.TestCase):
    def test_deadline_and_queue_identity_on_both_native_layouts(self):
        header = re.sub(r'^#(?:include|pragma once)[^\n]*\n', '', HEADER.read_text(), flags=re.M)
        source = re.sub(r'^#include[^\n]*\n', '', SOURCE.read_text(), flags=re.M)
        # The 64-bit host controls numeric timer handles but retains the exact
        # 20-byte legacy timer layout. Real OSI/pointer ABI is target-compiled.
        source = re.sub(r'_Static_assert\(sizeof\(uintptr_t\).*?;', '', source, count=1, flags=re.S)
        for c5 in (0, 1):
            with self.subTest(c5=c5):
                compile_run(self, '#define CONFIG_IDF_TARGET_ESP32C5 ' + str(c5) + '\n' +
                    TYPES + header + BOUNDARIES + source + MAIN)


TYPES = fixture_text('wifi/config/test_wifi_chm_timer/types.inc')

BOUNDARIES = fixture_text('wifi/config/test_wifi_chm_timer/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_chm_timer/main.inc')
