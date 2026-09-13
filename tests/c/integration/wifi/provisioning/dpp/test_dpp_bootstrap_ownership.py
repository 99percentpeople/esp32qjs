"""Deferred production bootstrap parsing/job ownership; SDK/eloop boundaries.

The 32-bit ABI assertion is omitted only for the host's wider uintptr_t. Numeric
high/low identities still exercise the production code. This is not RF proof.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
PART = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp/esp32_mquickjs_dpp_bootstrap.inc'


class DppBootstrapOwnership(unittest.TestCase):
    def test_strict_input_job_cancellation_stale_ticket_and_secret_release(self):
        source = PART.read_text().replace('_Static_assert(sizeof(uintptr_t) == 4, "review DPP eloop ticket encoding");', '')
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h').read_text()
        limits = '\n'.join(line for line in header.splitlines() if line.startswith('#define ESP32QJS_DPP_')) + '\n'
        compile_run(self, BOUNDARIES + limits + source + MAIN)


BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_dpp_bootstrap_ownership/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_dpp_bootstrap_ownership/main.inc')
