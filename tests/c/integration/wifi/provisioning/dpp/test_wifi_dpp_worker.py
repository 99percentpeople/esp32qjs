"""Deferred production DPP worker lifecycle, credential copies and IPC ownership."""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class DppWorker(unittest.TestCase):
    def test_native_calls_copy_commit_cleanup_retry_and_unknown_handoff(self):
        internal = ROOT / 'components/esp32_mquickjs/internal'
        header = declarations((internal / 'esp32_mquickjs_wifi_dpp_connection.h').read_text())
        header += declarations((internal / 'esp32_mquickjs_wifi_dpp_result.h').read_text())
        header += declarations((internal / 'esp32_mquickjs_wifi_dpp_worker.h').read_text())
        source = declarations((ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_worker.c').read_text())
        source = source.replace('_Static_assert(sizeof(dpp_ipc_config_t) == 12, "review DPP IPC ABI");', '')
        compile_run(self, TYPES + header + BOUNDARIES + source + MAIN)


TYPES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_worker/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_worker/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_worker/main.inc')
