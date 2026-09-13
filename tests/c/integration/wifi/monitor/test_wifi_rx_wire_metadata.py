"""Deferred production RX metadata encoder tests; no alternate encoder/state machine."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, COMMON, unit
from tests.support.native_compile import compile_run


class WiFiRxWireMetadata(unittest.TestCase):
    def test_wire_bytes_availability_layout_and_atomic_rejection(self):
        source = ''.join(unit(INTERNAL / name) for name in (
            'esp32_mquickjs_wifi_rx_wire.h', 'esp32_mquickjs_wifi_csi_layout.h',
            'esp32_mquickjs_wifi_rx_wire_metadata.h'))
        source += unit(COMMON / 'esp32_mquickjs_wifi_rx_wire.c')
        source += unit(COMMON / 'esp32_mquickjs_wifi_rx_wire_metadata.c')
        compile_run(self, PRELUDE + source + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
'''
MAIN = fixture_text('wifi/monitor/test_wifi_rx_wire_metadata/main.inc')
