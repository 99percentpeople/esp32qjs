"""Deferred production DPP TX capture, dispatch, cancellation and reuse.

Compiles the actual TX helper with controlled native/eloop boundaries during
the Wi-Fi test wave. This implementation batch only parses this Python file.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
PARTS = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp'


class DppTxControl(unittest.TestCase):
    def test_early_completion_dwell_queue_pressure_and_numeric_reuse(self):
        header = re.sub(r'^#(?:include|pragma once).*\n', '',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h').read_text(), flags=re.M)
        physical_header = re.sub(r'^#(?:include|pragma once).*\n', '',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_offchan_frame.h').read_text(), flags=re.M)
        compile_run(self, TYPES + header + physical_header + BOUNDARIES +
            (PARTS / 'esp32_mquickjs_dpp_tx.inc').read_text() + MAIN)


TYPES = fixture_text('wifi/provisioning/dpp/test_dpp_tx_control/types.inc')

BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_dpp_tx_control/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_dpp_tx_control/main.inc')
