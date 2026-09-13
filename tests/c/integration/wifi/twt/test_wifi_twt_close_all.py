"""Deferred real closeAll Future helpers over the production Radio registry.

Only SDK outcomes/retirement and background scheduling are injected. No claim
of full Future core/VM/GC/RF coverage; import/compile/run at Wi-Fi stage only.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.twt.test_wifi_twt_radio import INTERNAL, SOURCE
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiTwtCloseAll(unittest.TestCase):
    def test_snapshot_successors_cancel_and_retained_failure(self):
        source = (SOURCE.parent.parent / 'wifi_twt/esp32_mquickjs_wifi_twt_close.c').read_text()
        code = agreement_radio_code() + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
        begin = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[begin:source.index('};', begin) + 2]
        code += '\nbool esp32_mquickjs_wifi_twt_agreement_service(void){return false;}\n'
        for name in ('close_start', 'close_poll', 'close_cancel', 'close_destroy', 'close_timeout_ms'):
            code += extract(source, name)
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/twt/test_wifi_twt_close_all/main.inc')
