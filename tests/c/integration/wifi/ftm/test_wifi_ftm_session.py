"""Deferred production FTM Session plus actual Radio controller scheduling.

Inject SDK calls/storage, worker/timer/event scheduling, allocator and locks. Both
Session and Radio functions are production implementations, not a fixture FSM.
Write/AST only during API implementation; no SDK/RF/runtime acceptance implied.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.ftm.test_wifi_ftm_radio import ftm_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


def session_code(profile):
    code = ftm_code(profile, True)
    code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
    code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
    code = 'static void (*ftm_submit_hook)(void);\nstatic void (*ftm_report_hook)(void);\n' + code
    code = code.replace('++ftm_start_calls;if(ftm_early)ftm_event();return ftm_start_error;',
                        '++ftm_start_calls;if(ftm_submit_hook)ftm_submit_hook();if(ftm_early)ftm_event();return ftm_start_error;')
    code = code.replace('if(report_race)ftm_event();return 0;',
                        'if(ftm_report_hook)ftm_report_hook();if(report_race)ftm_event();return 0;')
    code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_ftm_session.h')
    code += BOUNDARIES
    code += unit(COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm_session.c')
    code += RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
    return code


class WiFiFtmSession(unittest.TestCase):
    def test_allocations_retained_reports_close_races_and_runtime_drain(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, session_code(profile) + MAIN)


BOUNDARIES = fixture_text('wifi/ftm/test_wifi_ftm_session/boundaries.inc')

MAIN = fixture_text('wifi/ftm/test_wifi_ftm_session/main.inc')
