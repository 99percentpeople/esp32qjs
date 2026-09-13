"""Deferred production probe retirement executor with real result and marker.

Only native SDK state/calls, timers, and event delivery are controlled. The
native quiescence predicate has its separate production SDK fixture. Do not
import, compile, or execute until the Wi-Fi API stage; AST is not runtime proof.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.twt.test_wifi_twt_probe_result import PRELUDE
from tests.c.integration.wifi.twt.test_wifi_twt_fence import BOUNDARIES as TIMER_BOUNDARIES
from tests.support.native_compile import compile_run


class WiFiTwtProbeRetire(unittest.TestCase):
    def test_pins_marker_event_ordering_failed_suffix_and_changed_cut(self):
        code = PRELUDE + '\n#include <stdatomic.h>\n'
        code += structure((COMPONENT / 'internal/esp32_mquickjs_wifi_twt_lane.h').read_text(),
                          'esp32_mquickjs_wifi_twt_token_t')
        code += structure((COMPONENT / 'internal/esp32_mquickjs_wifi_twt_sdk.h').read_text(),
                          'esp32_mquickjs_wifi_twt_probe_cut_t')
        code += TIMER_BOUNDARIES + BOUNDARIES
        for name in ('probe_result', 'fence', 'probe_retire'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        for name in ('probe_result', 'fence', 'probe_retire'):
            code += unit(COMPONENT / f'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_{name}.c')
        compile_run(self, code + MAIN)


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_probe_retire/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_probe_retire/main.inc')
