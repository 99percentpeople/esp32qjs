"""Deferred production ROC Session + Radio + Action ledger scheduling fixture.

Only SDK storage/events, background queue, clocks, allocator and lock boundaries
are injected. Tests do not replace the Session/Radio with a separate state machine.
No fixture is run during API implementation; real worker/SDK/RF testing follows.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_radio import radio_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiRocSession(unittest.TestCase):
    def test_budget_cancel_public_release_queue_fences_and_runtime_drain(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                code = radio_code(profile, True)
                # Radio snapshots legitimately take the critical lock without
                # taking the driver mutex. SDK calls still assert mutex ownership.
                code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
                code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
                code = 'static void (*roc_submit_hook)(void);\n' + code
                code = code.replace('++action_calls;request->op_id=sdk_id;return sdk_error;',
                                    '++action_calls;request->op_id=sdk_id;if(roc_submit_hook)roc_submit_hook();return sdk_error;')
                code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_roc_session.h')
                code += BOUNDARIES
                code += unit(COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_roc_session.c')
                code += RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
                compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_roc_session/boundaries.inc')
MAIN = fixture_text('wifi/tx/test_wifi_roc_session/main.inc')
