"""Deferred SmartConfig native credential/event ownership regression."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest

from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


class SmartConfigEvents(unittest.TestCase):
    def test_production_record_and_event_post_boundary(self):
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_smartconfig_events.h').read_text()
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_events.c').read_text()
        header = re.sub(r'^#(?:include|pragma)[^\n]*\n', '', header, flags=re.M)
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
        compile_run(self, BOUNDARIES + header + source + CASES)


BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_events/boundaries.inc')

CASES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_events/cases.inc')
