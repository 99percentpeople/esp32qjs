"""Deferred production broadcast Radio/Future tests with injected SDK scheduling.

Uses the actual lease registry, admission/status/close and shared Future helpers.
SDK outcomes and retirement proof are boundaries, not a substitute for RF, full
Future-core/VM/GC or real retirement-coordinator coverage. Do not run yet.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.twt.test_wifi_twt_agreement_future import agreement_future_code
from tests.support.native_compile import compile_run


class WiFiBroadcastAgreement(unittest.TestCase):
    def test_duplicate_stale_tokens_allocation_and_retirement(self):
        code = agreement_radio_code() + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
        compile_run(self, code + RADIO_CASES)

    def test_future_cancel_close_runtime_cleanup_and_native_completion(self):
        code = agreement_future_code()
        compile_run(self, code + FUTURE_CASES)


RADIO_CASES = fixture_text('wifi/twt/test_wifi_twt_broadcast_agreement/radio_cases.inc')

FUTURE_CASES = fixture_text('wifi/twt/test_wifi_twt_broadcast_agreement/future_cases.inc')
