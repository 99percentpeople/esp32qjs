"""Deferred production Dpp Future callbacks + full native Session source.

Only clock, Radio and worker scheduling boundaries are injected. Execute during
concentrated Wi-Fi validation; AST parsing is not a passing runtime test.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.provisioning.dpp.test_wifi_dpp_session import ROOT as COMPONENT, TYPES, BOUNDARIES, CASES, headers, unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class DppFuture(unittest.TestCase):
    def test_wait_cancellation_deadlines_and_native_retention(self):
        source = (COMPONENT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp.c').read_text()
        future = (COMPONENT / 'internal/esp32_mquickjs_future.h').read_text()
        boundaries = BOUNDARIES.replace('for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);', '')
        code = TYPES + headers() + boundaries
        code += unit(COMPONENT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c')
        code += CASES[:CASES.index('int main(void)')]
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', future).group(0)
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'static int JS_ThrowInternalError(JSContext *ctx,const char *s){(void)ctx;(void)s;return -1;}\n'
        code += 'static int dpp_error(JSContext*c,const char*n,esp32_mquickjs_wifi_dpp_session_t*s,int e,bool timeout){(void)c;(void)n;(void)s;(void)e;(void)timeout;return -1;}\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        code += re.search(r'typedef enum \{[^}]*\} dpp_operation_t;', source).group(0)
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        for name in ('dpp_destroy', 'dpp_start', 'dpp_poll', 'dpp_cancel', 'dpp_timeout'):
            code += extract(source, name)
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_future/main.inc')
