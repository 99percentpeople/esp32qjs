"""Deferred production Raw TX MAC allowlist/bounds/SDK-delegated policy tests."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit
from tests.support.native_compile import compile_run

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


class WiFiRawTxValidate(unittest.TestCase):
    def test_allowlist_short_spans_variable_headers_and_sdk_delegated_policy(self):
        source = unit(INTERNAL / 'esp32_mquickjs_wifi_rx.h')
        source += unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_validate.h')
        source += unit(COMMON / 'esp32_mquickjs_wifi_rx.c')
        source += unit(RAW / 'esp32_mquickjs_wifi_raw_tx_validate.c')
        for extended in (0,1):
            with self.subTest(extendedManagement=extended):
                compile_run(self, PRELUDE + source + MAIN,
                            cflags=["-DESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT="+str(extended)])


PRELUDE = fixture_text('wifi/tx/test_wifi_raw_tx_validate/prelude.inc')
MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_validate/main.inc')
