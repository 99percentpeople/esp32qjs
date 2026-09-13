"""Deferred actual FTM Future start/poll/cancel/destroy over Session and Radio.

Clock/SDK/worker scheduling are injected. Full Future-core settlement, JS class
handoff and device RF are separate gates; no fixture is executed this wave.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.ftm.test_wifi_ftm_session import session_code, MAIN as SESSION_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiFtmFuture(unittest.TestCase):
    def test_receive_deadline_end_and_close_have_distinct_native_effects(self):
        source = (COMPONENT / 'src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_future.h').read_text()
        code = session_code('esp32c5/representative')
        code += SESSION_MAIN[:SESSION_MAIN.index('int main(void)')]
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} '+name+';', header).group(0)
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += re.search(r'typedef enum \{[^}]*\} ftm_operation_t;', source).group(0)
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start)+3]
        code += ''.join(extract(source, name) for name in ('ftm_destroy', 'ftm_start', 'ftm_poll', 'ftm_cancel', 'ftm_timeout'))
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/ftm/test_wifi_ftm_future/main.inc')
