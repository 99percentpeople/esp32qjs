"""Deferred production Radio restart-event barrier; scheduler/SDK queue injected.

Does not prove the closed SDK's event emission or execute a physical band switch.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiBandCycleEvents(unittest.TestCase):
    def test_real_stop_start_order_late_stop_stale_marker_saturation_and_cancel(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = PRELUDE
        code += re.search(r'enum \{ RADIO_EVENTS_IDLE[^}]*\};', radio).group(0)
        code += re.search(r'typedef struct \{[^}]*\} wifi_radio_event_fence_t;', radio).group(0)
        code += BOUNDARIES
        for name in ('wifi_radio_lifecycle_fence', 'wifi_radio_observe_lifecycle_event',
                     'wifi_radio_begin_events', 'wifi_radio_wait_events_inner'):
            code += extract(radio, name)
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/config/test_wifi_band_cycle_events/prelude.inc')

BOUNDARIES = fixture_text('wifi/config/test_wifi_band_cycle_events/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_band_cycle_events/main.inc')
