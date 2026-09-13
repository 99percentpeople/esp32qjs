"""Deferred production observation polling, Future gating and lifetime checks.

Full Session source and real watch helpers; SDK/worker and EventQueue lifetime
operations are injected boundaries. Not a full Future-core/reaper scheduling or
VM constructor proof. Execute only in the concentrated Wi-Fi validation phase.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.provisioning.dpp.test_wifi_dpp_session import ROOT as COMPONENT, TYPES, BOUNDARIES, CASES, headers, unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class DppWatch(unittest.TestCase):
    def test_terminal_ordering_saturation_close_and_retained_budget(self):
        public = (COMPONENT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp.c').read_text()
        boundaries = BOUNDARIES.replace('assert(!locks);locks++;', 'assert(locks<2);locks++;')
        boundaries = boundaries.replace('assert(locks==1);locks--;', 'assert(locks);locks--;')
        boundaries = boundaries.replace('for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);', '')
        code = TYPES + headers() + boundaries
        code += unit(COMPONENT / 'src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_session.c')
        code += CASES[:CASES.index('int main(void)')]
        code += 'typedef struct mock_queue esp32_mquickjs_event_queue_t;\n'
        code += public[public.index('#define DPP_WATCH_HANDLES'):public.index('/* Compare semantic fields')]
        code += QUEUES
        for name in ('dpp_watch_same', 'dpp_watch_closed', 'dpp_watch_destroyed',
                     'esp32_mquickjs_wifi_dpp_poll_observations'):
            code += extract(public, name)
        compile_run(self, code + MAIN)


QUEUES = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_watch/queues.inc')

MAIN = fixture_text('wifi/provisioning/dpp/test_wifi_dpp_watch/main.inc')
