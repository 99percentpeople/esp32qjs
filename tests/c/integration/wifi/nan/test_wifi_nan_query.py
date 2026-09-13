"""Deferred production NAN cache queries, exact Radio tokens and bounded reads.

Only SDK storage/RTOS boundaries are supplied here. The query, matching and
conversion implementations are read from production files, never recreated.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

BASE = TEST_ROOT / 'components/esp32_mquickjs'


def query_types():
    # Public esp_nan.h metadata layout; C5 production compilation separately
    # checks the actual SDK definition (security/pairing do not change it).
    source = fixture_text('wifi/nan/test_wifi_nan_query/query_types-source.inc')
    source += (BASE / 'internal/esp32_mquickjs_wifi_nan_query.h').read_text()
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


def compile_and_run(test, code):
    cc = shutil.which('cc')
    if not cc:
        test.skipTest('Host C compiler unavailable')
    with tempfile.TemporaryDirectory() as folder:
        src, exe = Path(folder) / 'case.c', Path(folder) / 'case'
        src.write_text(code)
        result = subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            str(src), '-o', str(exe)], text=True, capture_output=True, timeout=60)
        test.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(exe)], text=True, capture_output=True, timeout=10)
        test.assertEqual(result.returncode, 0, result.stderr)


class NanQuery(unittest.TestCase):
    def test_actual_native_snapshot_service_scope_zeroing_limits_and_corruption(self):
        production = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_query_sdk.inc').read_text()
        production = re.sub(r'^#include[^\n]*\n', '', production, flags=re.M)
        compile_and_run(self, SDK_PREFIX + query_types() + SDK_STORAGE + production + SDK_MAIN)

    def test_actual_radio_query_rejects_stale_identity_closing_and_usd(self):
        from tests.c.integration.wifi.nan.test_wifi_nan_radio_retirement import function
        production = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_nan_radio.inc').read_text()
        code = SDK_PREFIX + query_types() + RADIO_BOUNDARY
        code += function(production, 'wifi_radio_nan_exact_locked')
        code += function(production, 'esp32_mquickjs_wifi_radio_nan_query')
        compile_and_run(self, code + RADIO_MAIN)


SDK_PREFIX = fixture_text('wifi/nan/test_wifi_nan_query/sdk_prefix.inc')

SDK_STORAGE = fixture_text('wifi/nan/test_wifi_nan_query/sdk_storage.inc')

SDK_MAIN = fixture_text('wifi/nan/test_wifi_nan_query/sdk_main.inc')

RADIO_BOUNDARY = fixture_text('wifi/nan/test_wifi_nan_query/radio_boundary.inc')

RADIO_MAIN = fixture_text('wifi/nan/test_wifi_nan_query/radio_main.inc')
