"""Deferred production information operation/result ownership at SDK boundaries.

Not a replacement state machine. This compiles the production source when the
Wi-Fi stage runs; only allocator/lock/task/TX/timer/event functions are injected.
No claim about SDK queue ordering, full Future core, GC, or RF interoperability.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiTwtInformationOperation(unittest.TestCase):
    def test_exact_tx_completion_abandon_queue_failure_and_exhaustion(self):
        code = PRELUDE
        for name in ('information_timer', 'information'):
            code += unit(COMPONENT / f'internal/esp32_mquickjs_wifi_twt_{name}.h')
        code += BOUNDARIES
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_information.c')
        compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/twt/test_wifi_twt_information_operation/prelude.inc')
BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_information_operation/boundaries.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_information_operation/main.inc')
