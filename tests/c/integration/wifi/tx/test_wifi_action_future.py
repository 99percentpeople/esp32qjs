"""Deferred production Action Future polling with real Radio status/ledger.

Worker scheduling is injected. Physical termination is supplied at the native
attestation boundary; test_wifi_action_recovery covers its actual Radio producer.
This is not full Future-core, VM or SDK concurrency coverage.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_radio import radio_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiActionFuture(unittest.TestCase):
    def test_physical_termination_waits_for_submission_publication_and_is_not_success(self):
        source = (COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_future.h').read_text()
        code = '#include <stdatomic.h>\n' + radio_code('esp32c5/representative', True)
        code = code.replace('assert(locks && !critical);critical=1;', 'assert(!critical);critical=1;')
        code = code.replace('assert(locks && critical);critical=0;', 'assert(critical);critical=0;')
        code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_future_poll_t;', header).group(0)
        code += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        code += source[start:source.index('\n};', start) + 3]
        code += 'static void action_schedule(esp32_mquickjs_future_driver_state_t *state) {assert(state->submitted);}\n'
        code += extract(source, 'action_poll')
        code += RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]
        compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/tx/test_wifi_action_future/main.inc')
