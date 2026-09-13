"""Deferred production TX snapshot using pinned target declarations and guard pages."""
from tests.support.fixtures import fixture_text
import json
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, unit
from tests.support.native_compile import compile_run

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


class WiFiRawTxSnapshot(unittest.TestCase):
    def test_callback_pointers_are_copied_without_using_data_or_body_report_as_a_span(self):
        inventory = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']
        for profile in ['esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative']:
            with self.subTest(profile=profile):
                symbols = {key.split('::')[-1]: value['declaration'] for key, value in inventory[profile]['symbols'].items()}
                source = PRELUDE
                for name in ['wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t']:
                    source += symbols[name] + ';\n'
                for name in ['esp32_mquickjs_wifi_rx.h', 'esp32_mquickjs_wifi_raw_tx_validate.h', 'esp32_mquickjs_wifi_raw_tx_snapshot.h']:
                    source += unit(INTERNAL / name)
                source += unit(RAW / 'esp32_mquickjs_wifi_raw_tx_snapshot.c')
                compile_run(self, source + MAIN)


PRELUDE = fixture_text('wifi/tx/test_wifi_raw_tx_snapshot/prelude.inc')
MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_snapshot/main.inc')
