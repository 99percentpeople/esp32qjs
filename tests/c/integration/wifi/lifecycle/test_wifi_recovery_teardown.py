"""Deferred real runtime destruction, Future prepare and recovery cleanup order.

Composes production core destruction, Future slot teardown, shared recovery poll/
cancel/dispose and runtime cleanup. VM resources, unrelated native Future and
Radio/worker completion are injected boundaries. Native physical proof itself is
covered by Action/FTM Radio fixtures. No fixture import/compile/run in API phase.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.lifecycle.test_wifi_recovery_runtime import recovery_runtime_code
from tests.c.integration.wifi.lifecycle.test_wifi_recovery_future import BOUNDARIES as CLOCK
from tests.c.integration.wifi.config.test_wifi_configuration_cleanup import WIFI
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, INTERNAL, extract


def teardown_code():
    code = '#define esp32_mquickjs_memory_payload_free heap_caps_free\n' + recovery_runtime_code(1)
    future = (CORE / 'esp32_mquickjs_future.c').read_text()
    recovery = (WIFI.parent.parent / 'wifi_common/esp32_mquickjs_wifi_recovery.c').read_text()
    header = (INTERNAL / 'esp32_mquickjs_future.h').read_text()
    for name in ('esp32_mquickjs_future_poll_t', 'esp32_mquickjs_cancel_result_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += '\n' + re.search(r'^#define ESP32_MQUICKJS_FUTURE_DRIVER_CHUNK_SIZE .*$', future, re.M).group(0) + '\n'
    code += TYPES
    start = recovery.index('struct esp32_mquickjs_future_driver_state {')
    code += recovery[start:recovery.index('\n};', start) + 3]
    code += CLOCK
    code += ''.join(extract(recovery, name) for name in ('recovery_start', 'recovery_poll', 'recovery_cancel'))
    start = future.index('typedef enum {\n    FUTURE_STATE_QUEUED,')
    code += future[start:future.index('} future_runtime_t;', start) + len('} future_runtime_t;')]
    code += BOUNDARIES
    for name in ('future_runtime', 'future_is_terminal', 'future_clear_driver_registry', 'future_release_call_refs',
                 'future_release_input_refs', 'future_stop_timer', 'future_destroy_driver', 'future_clear_slot'):
        code += extract(future, name)
    code += extract((CORE / 'esp32_mquickjs_future_scheduler.c').read_text(),
                    'esp32_mquickjs_future_scheduler_teardown_must_wait')
    code += extract(future, 'esp32_mquickjs_prepare_future_runtime_destroy')
    code += extract(WIFI.read_text(), 'esp32_mquickjs_prepare_wifi_recovery_runtime_destroy')
    code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_destroy_internal')
    return code


class WiFiRecoveryTeardown(unittest.TestCase):
    def test_cancelled_recovery_progresses_while_other_future_or_original_owner_waits(self):
        compile_run(self, teardown_code() + MAIN)


TYPES = fixture_text('wifi/lifecycle/test_wifi_recovery_teardown/types.inc')

BOUNDARIES = fixture_text('wifi/lifecycle/test_wifi_recovery_teardown/boundaries.inc')

MAIN = fixture_text('wifi/lifecycle/test_wifi_recovery_teardown/main.inc')
