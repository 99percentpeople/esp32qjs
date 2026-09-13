"""Deferred execution of the production DPP eloop ticket registry.

Only the allocator and native scheduler are controlled. AST checks do not
constitute execution or Wi-Fi/RTOS qualification.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
PART = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp/esp32_mquickjs_dpp_async.inc'


class DppAsyncOwnership(unittest.TestCase):
    def test_detached_cancel_payload_lifetime_stale_tickets_and_bounded_admission(self):
        compile_run(self, BOUNDARIES + PART.read_text() + MAIN)


BOUNDARIES = fixture_text('wifi/provisioning/dpp/test_dpp_async_ownership/boundaries.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_dpp_async_ownership/main.inc')
