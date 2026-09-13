"""Deferred production DPP Session scheduling, result retention and close.

The real Session controls scheduling and ownership. Native Radio, runtime
Station helper, allocation and clock are injectable boundaries. AST only until
the concentrated Wi-Fi test wave; no RF/RTOS proof is implied.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT / 'components/esp32_mquickjs'


def unit(path):
    source = path.read_text()
    if path.name == 'esp32_mquickjs_wifi_dpp_session.c':
        source = source.replace('#include "esp32_mquickjs_wifi_dpp_session_connection.inc"',
            path.with_name('esp32_mquickjs_wifi_dpp_session_connection.inc').read_text())
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', source, flags=re.M)


def headers():
    return ''.join(unit(ROOT / 'internal' / f'esp32_mquickjs_wifi_dpp_{name}.h')
        for name in ('connection', 'result', 'worker', 'radio', 'station', 'session'))


class DppSession(unittest.TestCase):
    def test_results_worker_storage_and_independent_close_suffixes(self):
        headers = ''.join(unit(ROOT / 'internal' / f'esp32_mquickjs_wifi_dpp_{name}.h')
            for name in ('connection', 'result', 'worker', 'radio', 'station', 'session'))
        compile_run(self, TYPES + headers + BOUNDARIES +
            unit(ROOT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c') + CASES)


TYPES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_session/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_session/boundaries.inc')

CASES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_session/cases.inc')
