"""Deferred full production WPS Session scheduler, result and teardown cases.

AST only during implementation. Radio/Station/clock/worker queue are injected
boundaries; the production Session controls the ordering and native storage.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from tests.support.idf import require_idf
import os
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
COMPONENT = ROOT / 'components/esp32_mquickjs'


def unit(path):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', path.read_text(), flags=re.M)


def headers():
    sdk = require_idf()
    return unit(sdk / 'components/wpa_supplicant/esp_supplicant/include/esp_wps.h') + ''.join(
        unit(COMPONENT / 'internal' / ('esp32_mquickjs_wifi_wps_' + name + '.h'))
        for name in ('sdk', 'worker', 'radio', 'station', 'session'))


class WPSSession(unittest.TestCase):
    def test_production_session_worker_and_helper_retirement(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        code = TYPES + headers()
        code += BOUNDARIES
        code += unit(COMPONENT / 'src/modules/wifi_wps/esp32_mquickjs_wifi_wps_session.c')
        compile_run(self, code + CASES)


TYPES = fixture_text('wifi/provisioning/wps/test_wifi_wps_session/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/wps/test_wifi_wps_session/boundaries.inc')

CASES = fixture_text('wifi/provisioning/wps/test_wifi_wps_session/cases.inc')
