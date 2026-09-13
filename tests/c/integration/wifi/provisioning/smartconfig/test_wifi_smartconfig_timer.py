"""Deferred production SmartConfig timer ownership and SDK dispatch tests."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


class SmartConfigTimers(unittest.TestCase):
    def test_production_timer_registry_and_wrappers(self):
        paths = [
            'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_timer.h',
            'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_timer.c',
            'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_timer_sdk.c',
        ]
        production = ''.join(re.sub(r'^#(?:include|pragma)[^\n]*\n', '', (ROOT / p).read_text(), flags=re.M) for p in paths)
        compile_run(self, BOUNDARIES + production + CASES)


BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_timer/boundaries.inc')

CASES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_timer/cases.inc')
