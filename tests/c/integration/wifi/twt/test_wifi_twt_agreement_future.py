"""Deferred real Agreement worker/poll/cancel/cleanup over production Radio.

Native SDK boundaries and worker scheduling are injected. These cases do not
claim full Future core, JS object handoff, movable GC or real-device evidence.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.twt.test_wifi_twt_radio import INTERNAL, SOURCE as RADIO_SOURCE
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def agreement_future_code():
    source = (RADIO_SOURCE.parent.parent / 'wifi_twt/esp32_mquickjs_wifi_twt_agreement.c').read_text()
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    code = agreement_radio_code() + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
    code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
    code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += source[source.index('typedef struct {'):source.index('} twt_agreement_handle_t;') + len('} twt_agreement_handle_t;')]
    start = source.index('struct esp32_mquickjs_future_driver_state {')
    code += source[start:source.index('#define SET', start)]
    code += BOUNDARIES
    for name in ('agreement_request_close', 'agreement_present', 'broadcast_setup_accepted', 'agreement_cleanup_worker', 'esp32_mquickjs_wifi_twt_agreement_service',
                 'esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy', 'agreement_submit_worker',
                 'agreement_schedule', 'agreement_start', 'agreement_poll', 'agreement_cancel', 'agreement_destroy', 'agreement_timeout'):
        code += extract(source, name)
    return code


class WiFiTwtAgreementFuture(unittest.TestCase):
    def test_cancelled_submit_and_multi_owner_runtime_cleanup(self):
        compile_run(self, agreement_future_code() + MAIN)


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_agreement_future/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_agreement_future/main.inc')
