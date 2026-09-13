"""Deferred production observation polling, Future gating and lifetime checks.

Full Session source and real watch helpers; SDK/worker and EventQueue lifetime
operations are injected boundaries. Not a full Future-core/reaper scheduling or
VM constructor proof. Execute only in the concentrated Wi-Fi validation phase.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.provisioning.smartconfig.test_wifi_smartconfig_session import COMPONENT, TYPES, BOUNDARIES, CASES, headers, unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class SmartConfigWatch(unittest.TestCase):
    def test_terminal_ordering_saturation_close_and_retained_budget(self):
        public = (COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig.c').read_text()
        boundaries = BOUNDARIES.replace('assert(!locks);locks=1;', 'assert(locks<2);locks++;')
        boundaries = boundaries.replace('assert(locks);locks=0;', 'assert(locks);locks--;')
        code = TYPES + headers() + boundaries
        code += unit(COMPONENT / 'src/modules/wifi_smartconfig/esp32_mquickjs_wifi_smartconfig_session.c')
        code += CASES[:CASES.index('int main(void)')]
        code += 'typedef struct mock_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define SC_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        code += QUEUES
        for name in ('sc_watch_same', 'sc_watch_closed', 'sc_watch_destroyed',
                     'esp32_mquickjs_wifi_smartconfig_poll_observations'):
            code += extract(public, name)
        compile_run(self, code + MAIN)


QUEUES = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_watch/queues.inc')

MAIN = fixture_text('wifi/provisioning/smartconfig/test_wifi_smartconfig_watch/main.inc')
