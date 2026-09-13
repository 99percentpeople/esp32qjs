"""Deferred full production SmartConfig Session worker/lifetime regressions."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
COMPONENT = ROOT / 'components/esp32_mquickjs'


def unit(path):
    return re.sub(r'^#(?:include|pragma)[^\n]*\n', '', path.read_text(), flags=re.M)


def headers():
    return ''.join(unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_smartconfig_' + name + '.h'))
                   for name in ('events', 'decoder', 'radio', 'connection', 'session'))


class SmartConfigSession(unittest.TestCase):
    def test_production_service_and_runtime_teardown(self):
        production = unit(COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c')
        compile_run(self, TYPES + headers() + BOUNDARIES + production + CASES)


TYPES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_session/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_session/boundaries.inc')

CASES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_session/cases.inc')
