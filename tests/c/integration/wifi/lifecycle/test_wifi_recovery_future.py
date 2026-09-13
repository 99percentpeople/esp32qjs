"""Deferred real recovery Future polling over the production runtime stepper.

The clock and wait-scope are controllable; Radio/SDK boundaries come from the
runtime fixture. Future-core dispatch/timeout settlement remains a separate gate.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.lifecycle.test_wifi_recovery_runtime import recovery_runtime_code
from tests.c.integration.wifi.config.test_wifi_configuration_cleanup import WIFI
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiRecoveryFuture(unittest.TestCase):
    def test_recovery_yields_to_original_owner_and_cancel_does_not_resume(self):
        source = (WIFI.parent.parent / 'wifi_common/esp32_mquickjs_wifi_recovery.c').read_text()
        header = (WIFI.parents[3] / 'internal/esp32_mquickjs_future.h').read_text()
        code = recovery_runtime_code(1)
        for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
            code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
        code += 'typedef int JSContext,esp32_mquickjs_runtime_t,esp32_mquickjs_future_token_t;\n'
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += BOUNDARIES
        code += ''.join(extract(source, name) for name in ('recovery_start', 'recovery_poll', 'recovery_cancel'))
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_recovery_future/boundaries.inc')

MAIN = fixture_text('wifi/lifecycle/test_wifi_recovery_future/main.inc')
