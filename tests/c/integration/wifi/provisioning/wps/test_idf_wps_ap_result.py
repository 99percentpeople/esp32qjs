"""Deferred production AP WPS result-owner and queue-pressure regressions.

Only AST-parse in the implementation wave. The entire production include is
compiled here later; allocator, SDK admission and event-post are boundaries.
No claim of native drain, public Future or RF completion.
"""
from tests.support.paths import ROOT as TEST_ROOT
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import unittest
from tests.support.native_compile import compile_run

ROOT = TEST_ROOT
HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_result.h'
SOURCE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_result.inc'


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class WpsAPResult(unittest.TestCase):
    def run_case(self, main):
        compile_run(self, PRELUDE + declarations(HEADER.read_text()) +
                    declarations(SOURCE.read_text()) + POST + main)

    def test_queue_full_cannot_discard_terminal_or_pin_and_old_id_cannot_resolve(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_result/test_queue_full_cannot_discard_terminal_or_pin_and_old_id_cannot_resolve.inc'))

    def test_managed_pin_secret_and_result_survive_sdk_detach_without_queue_publication(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_result/test_managed_pin_secret_and_result_survive_sdk_detach_without_queue_publication.inc'))

    def test_close_intent_stale_input_malformed_payload_and_first_error(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_result/test_close_intent_stale_input_malformed_payload_and_first_error.inc'))

    def test_admission_oom_thread_identity_exhaustion_and_unbound_discard(self):
        self.run_case(fixture_text('wifi/provisioning/wps/test_idf_wps_ap_result/test_admission_oom_thread_identity_exhaustion_and_unbound_discard.inc'))


PRELUDE = fixture_text('wifi/provisioning/wps/test_idf_wps_ap_result/prelude.inc')

POST = fixture_text('wifi/provisioning/wps/test_idf_wps_ap_result/post.inc')
