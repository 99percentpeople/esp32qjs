"""Deferred tests of the production sole-v1 envelope layout/control encoder."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import INTERNAL, COMMON, unit
from tests.support.native_compile import compile_run


class WiFiRxWire(unittest.TestCase):
    def test_canonical_offsets_atomic_failure_lengths_and_uint32_overflow(self):
        source = unit(INTERNAL / 'esp32_mquickjs_wifi_rx_wire.h')
        source += unit(COMMON / 'esp32_mquickjs_wifi_rx_wire.c')
        compile_run(self, PRELUDE + source + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
'''
MAIN = fixture_text('wifi/monitor/test_wifi_rx_wire/main.inc')
