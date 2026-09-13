"""Deferred production FIFO/ownership/fenced ledger tests; no replacement model."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, unit
from tests.support.native_compile import compile_run


class WiFiRawTxQueue(unittest.TestCase):
    def test_atomic_batches_protected_active_batch_and_flush_fences(self):
        code = PRELUDE
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_queue.h')
        code += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_queue.c')
        compile_run(self, code + HELPERS + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
'''

HELPERS = fixture_text('wifi/tx/test_wifi_raw_tx_queue/helpers.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_queue/main.inc')
