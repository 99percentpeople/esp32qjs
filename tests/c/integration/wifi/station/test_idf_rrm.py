"""Deferred production SDK RRM regression; execute with the Wi-Fi phase suite.

The SDK request, response, timeout and reset bodies are compiled unchanged (or
through the production hash-gated patch). Only allocation, TX, timer, profile
and observation boundaries are injected. This file is not a replacement FSM.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
import os
import pathlib
import re
import sys
import unittest

from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_rrm import patch_source


def sdk_function(source, name):
    match = re.search(r'^(?:static )?(?:void|int|esp_err_t) ' + name
                      + r'\([^;{}]*\)\n\{', source, re.M)
    if not match:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.start()) + 3]


class IDFRRM(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            raise unittest.SkipTest('Set IDF_PATH to the reviewed ESP-IDF')
        component = pathlib.Path(sdk) / 'components/wpa_supplicant'
        cls.source = (component / 'src/common/rrm.c').read_bytes()
        cls.patched = patch_source(cls.source)
        cls.public = (component / 'esp_supplicant/src/esp_common.c').read_text()

    def test_source_drift_and_double_patch_rejected(self):
        for source in [self.source + b'\n', self.patched]:
            with self.subTest(length=len(source)), self.assertRaises(ValueError):
                patch_source(source)

    def test_production_request_response_timeout_and_exact_retirement(self):
        for patched, content in [(False, self.source), (True, self.patched)]:
            with self.subTest(patched=patched):
                source = content.decode()
                body = 'static bool *esp32qjs_rrm_tx_attempted;\n' if patched else ''
                for name in ['wpas_rrm_neighbor_rep_timeout_handler', 'wpas_rrm_reset',
                             'wpas_rrm_process_neighbor_rep', 'wpas_rrm_send_neighbor_rep_request']:
                    body += sdk_function(source, name)
                body += sdk_function(self.public, 'esp_rrm_send_neighbor_report_request')
                if patched:
                    body += 'typedef void (*esp32qjs_rrm_callback_t)(void *, const u8 *, size_t);\n'
                    for name in ['esp32qjs_rrm_query', 'esp32qjs_rrm_request',
                                 'esp32qjs_rrm_cancel', 'esp32qjs_rrm_publish_observation']:
                        body += sdk_function(source, name)
                compile_run(self, f'#define PATCHED {int(patched)}\n' + BOUNDARIES + body + MAIN)


class RRMDispatch(unittest.TestCase):
    def test_production_dispatch_keeps_sdk_result_and_unknown_ownership_separate(self):
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_rrm_sdk.h').read_text()
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_roaming/esp32_mquickjs_wifi_rrm_sdk.c').read_text()
        types = header[header.index('typedef void'):header.index('/* Internal only.')]
        call_type = source[source.index('typedef struct {'):source.index('} rrm_sdk_call_t;') + len('} rrm_sdk_call_t;')]
        compile_run(self, DISPATCH_BOUNDARIES + types + call_type
                    + sdk_function(source, 'rrm_sdk_dispatch')
                    + sdk_function(source, 'esp32_mquickjs_wifi_rrm_sdk_command')
                    + DISPATCH_MAIN)


DISPATCH_BOUNDARIES = fixture_text('wifi/station/test_idf_rrm/dispatch_boundaries.inc')

DISPATCH_MAIN = fixture_text('wifi/station/test_idf_rrm/dispatch_main.inc')


BOUNDARIES = fixture_text('wifi/station/test_idf_rrm/boundaries.inc')

MAIN = fixture_text('wifi/station/test_idf_rrm/main.inc')
