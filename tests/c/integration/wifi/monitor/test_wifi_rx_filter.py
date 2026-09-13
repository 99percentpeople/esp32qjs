"""Deferred production RX target/parser/filter tests; no replacement state machine."""
from tests.support.fixtures import fixture_text
import unittest

import tests.c.integration.wifi.monitor.test_wifi_rx_target as rx_target
from tests.support.native_compile import compile_run


class WiFiRxFilter(unittest.TestCase):
    def test_address_roles_masks_errors_decimation_and_monotonic_rate(self):
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile):
                code = rx_target.WiFiRxTarget().production_code(profile)
                code += rx_target.unit(rx_target.INTERNAL / 'esp32_mquickjs_wifi_rx_filter.h')
                code += rx_target.unit(rx_target.COMMON / 'esp32_mquickjs_wifi_rx_filter.c') + MAIN
                compile_run(self, code)


MAIN = fixture_text('wifi/monitor/test_wifi_rx_filter/main.inc')
