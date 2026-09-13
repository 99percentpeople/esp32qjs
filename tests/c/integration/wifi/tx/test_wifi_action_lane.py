"""Deferred production Action/ROC record with SDK/event boundary injection.

No substitute state machine. SDK enum definitions come from the reviewed live
inventory. Driver/event-loop fence calls remain future integration boundaries;
this fixture does not qualify SDK termination, Radio admission or FreeRTOS races.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run


class WiFiActionLane(unittest.TestCase):
    def test_early_events_two_stage_completion_cancel_suffix_and_exact_fences(self):
        for target in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(target=target):
                code = PRELUDE + sdk_types(target, ('wifi_action_tx_status_type_t', 'wifi_roc_done_status_t'))
                code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_action_lane.h')
                code += unit(COMPONENT / 'src/modules/wifi_action/esp32_mquickjs_wifi_action_lane.c')
                compile_run(self, code + MAIN)


PRELUDE = fixture_text('wifi/tx/test_wifi_action_lane/prelude.inc')
MAIN = fixture_text('wifi/tx/test_wifi_action_lane/main.inc')
